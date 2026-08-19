# NightCrawler — MeshCore protocol surface

This document describes **only** the slice of MeshCore that NightCrawler depends
on. Unlike the first draft of this spec, the opcodes and request types below are
no longer guesses: they were **read directly from the MeshCore firmware source**
(`meshcore-dev/MeshCore`, build **v1.17.1**, files
`examples/companion_radio/MyMesh.cpp`, `examples/simple_repeater/MyMesh.cpp`,
`src/Packet.h`, `src/helpers/RegionMap.*`).

> ⚠️ **Still keep them in one place.** The values below are correct for v1.17.1,
> but MeshCore does bump opcodes between builds. NightCrawler reads every numeric
> constant from a single `OpCodes` file (see [ARCHITECTURE.md](ARCHITECTURE.md))
> so a firmware bump is a one-file change. Before a run, confirm the companion's
> reported firmware (`CMD_DEVICE_QUERY` → `FIRMWARE_VER_CODE`) against the build
> these constants were taken from.

---

## 0. The one thing that shaped v0.1: what a node answers, and to whom

MeshCore gates remote reads into **three tiers**. This is the single most
important fact for NightCrawler, because it decides what the crawler can learn
and at what cost. Verified against `simple_repeater/MyMesh.cpp`:

| Tier | How it arrives | What you can read | Login needed? |
|---|---|---|---|
| **Anonymous** | `PAYLOAD_TYPE_ANON_REQ` → `onAnonDataRecv()` | node **name + `owner.info`**, **flood-allowed scopes/regions**, remote **clock** | **No** — none |
| **Guest** | `PAYLOAD_TYPE_REQ` → `onPeerDataRecv()` → `handleRequest()` | **neighbours**, **firmware version**, status/`RepeaterStats`, base telemetry | **Yes** — any successful login (guest is enough) |
| **Admin** | text CLI over `PAYLOAD_TYPE_TXT_MSG` (gated by `client->isAdmin()`); `REQ_TYPE_GET_ACCESS_LIST` | ACL, full `region` config, every `set`/`get` CLI command | **Yes** — admin login |

NightCrawler **v0.1 uses the Anonymous + Guest tiers only. It never attempts an
admin login and never sends a text CLI command.** That single rule flows through
every design decision below.

Two consequences worth stating up front, because they overturn assumptions the
first draft made:

- **Firmware version is not anonymous — by design.** It was deliberately removed
  from the anonymous responses (firmware commit `b6110eee`, 2026-01-12, comment:
  *"ANON_REQ_TYPE_OWNER, firmware-ver removed (security exploit)"*). The full
  `ver` string is only returned by the **guest** request `REQ_TYPE_GET_OWNER_INFO`.
- **`neighbors`, `ver`, `get owner.info`, `region list` are admin CLI text
  commands** (`onPeerDataRecv` only accepts `PAYLOAD_TYPE_TXT_MSG` when
  `client->isAdmin()`). A **guest** cannot use them. Guests read the same facts
  through the **structured binary requests** (`REQ_TYPE_*`) instead — which is
  what NightCrawler does.

---

## 1. The companion connection (TCP/IP)

A MeshCore **companion** node runs a firmware role designed to be driven by a
client app over the **Companion Radio Protocol**. That protocol is transport-
agnostic; the same frames run over Serial/USB, BLE (Nordic UART) and — the one
NightCrawler uses — **TCP/IP** via the ESP32 WiFi companion
(`SerialWifiInterface`).

### Framing (serial + TCP/WiFi)

The frame *payload* is identical on every transport:

```
[ 1 byte: command / response / push code ][ variable: little-endian data, UTF-8 strings ]
```

On the serial and TCP/WiFi transports frames are **length-delimited**:

- **device → app:** `>` (`0x3E`) followed by a **16-bit little-endian length**,
  then the payload.
- **app → device:** `<` (`0x3C`) followed by the payload.

(BLE, which we don't use, is one frame per characteristic write/notification with
no length prefix.)

The default TCP port by convention is **5000**.

### Handshake

Typical startup, which NightCrawler follows before it crawls anything:

1. `CMD_APP_START` (1) → expect `RESP_CODE_SELF_INFO` (5) — our companion's
   identity, name, public key, channel/radio params.
2. `CMD_DEVICE_QUERY` (22) → `RESP_CODE_DEVICE_INFO` (13) — firmware/version and
   `FIRMWARE_VER_CODE` of *our* companion (pin opcodes against this).
3. `CMD_GET_DEVICE_TIME` (5) / `CMD_SET_DEVICE_TIME` (6) as needed (clock sanity).
4. `CMD_GET_CONTACTS` (4) → a stream of `RESP_CODE_CONTACT` (3) between
   `RESP_CODE_CONTACTS_START` (2) and `RESP_CODE_END_OF_CONTACTS` (4) — the
   companion's known contacts (nodes it has heard adverts from). These are the
   crawl's **seed set** (FR-5, FR-7).

## 2. Companion frames NightCrawler uses

Verified `CMD_*` / `RESP_CODE_*` / `PUSH_CODE_*` values (decimal / hex) from
`examples/companion_radio/MyMesh.cpp` @ v1.17.1.

### App → firmware (`CMD_*`)

| Name | # | Purpose in NightCrawler |
|---|---:|---|
| `CMD_APP_START` | 1 | Open the session. |
| `CMD_DEVICE_QUERY` | 22 | Read the companion's own device/version info; pin opcodes. |
| `CMD_GET_CONTACTS` | 4 | Enumerate known contacts (seeds). Local, no airtime. |
| `CMD_GET_CONTACT_BY_KEY` | 30 | Fetch one contact by public key. Local. |
| `CMD_GET_DEVICE_TIME` / `CMD_SET_DEVICE_TIME` | 5 / 6 | Clock sync. Local. |
| `CMD_GET_ADVERT_PATH` | 42 | Read the cached path to a contact (do we have a route?). Local. |
| `CMD_SEND_PATH_DISCOVERY_REQ` | 52 | Discover a path to a node we have no route to. **OTA.** |
| `CMD_SEND_ANON_REQ` | 57 | **The anonymous-tier workhorse** — owner / regions / clock. **OTA.** |
| `CMD_SEND_LOGIN` | 26 | Guest login to a repeater/room server. **OTA.** |
| `CMD_SEND_BINARY_REQ` | 50 | **The guest-tier workhorse** — status / neighbours / owner-info / telemetry, by `REQ_TYPE`. **OTA.** |
| `CMD_LOGOUT` | 29 | Drop a guest session (tidy up). |
| `CMD_SEND_TRACE_PATH` | 36 | Path trace (per-hop SNR) — optional enrichment. **OTA.** |

### Firmware → app (`RESP_CODE_*` immediate, `PUSH_CODE_*` async)

| Name | # | Meaning |
|---|---:|---|
| `RESP_CODE_OK` / `RESP_CODE_ERR` | 0 / 1 | Command ack; `ERR` carries a one-byte code (`ERR_CODE_UNSUPPORTED_CMD`, `_NOT_FOUND`, `_TABLE_FULL`, `_BAD_STATE`, `_FILE_IO_ERROR`, `_ILLEGAL_ARG`). |
| `RESP_CODE_SELF_INFO` | 5 | Our companion's identity (reply to `CMD_APP_START`). |
| `RESP_CODE_DEVICE_INFO` | 13 | Device/version info (reply to `CMD_DEVICE_QUERY`). |
| `RESP_CODE_CONTACTS_START` / `RESP_CODE_CONTACT` / `RESP_CODE_END_OF_CONTACTS` | 2 / 3 / 4 | Contact enumeration stream. |
| `RESP_CODE_SENT` | 6 | An OTA request was launched. **Carries the 4-byte `tag`** you match the eventual async reply against, plus an estimated timeout. |
| `RESP_CODE_CURR_TIME` | 9 | Device clock. |
| `PUSH_CODE_LOGIN_SUCCESS` / `PUSH_CODE_LOGIN_FAIL` | 0x85 / 0x86 | Result of `CMD_SEND_LOGIN`. `SUCCESS` includes the permission byte and `FIRMWARE_VER_LEVEL`. |
| `PUSH_CODE_BINARY_RESPONSE` | 0x8C | **The reply to `CMD_SEND_ANON_REQ` and `CMD_SEND_BINARY_REQ`.** `[reserved][tag:4][payload]` — match `tag` to the `RESP_CODE_SENT` tag. |
| `PUSH_CODE_STATUS_RESPONSE` | 0x87 | Legacy status-response path (older `CMD_SEND_STATUS_REQ`). |
| `PUSH_CODE_TELEMETRY_RESPONSE` | 0x8B | Telemetry payload (CayenneLPP — big-endian, the one exception to the little-endian rule). |
| `PUSH_CODE_PATH_DISCOVERY_RESPONSE` | 0x8D | Result of a path-discovery request (`out_path` / `in_path`). |
| `PUSH_CODE_TRACE_DATA` | 0x89 | Path-trace hops with SNR. |
| `PUSH_CODE_ADVERT` / `PUSH_CODE_NEW_ADVERT` | 0x80 / 0x8A | An advert arrived — a node announced itself. Fold opportunistically into the graph and the frontier. |
| `PUSH_CODE_PATH_UPDATED` | 0x81 | A path to a contact changed. |

Push codes have the high bit set (`0x80+`) and can arrive **at any time**,
unsolicited. The client's read loop must always be draining frames and routing
pushes independently of whatever command it is currently awaiting. Because both
anonymous and guest requests are **asynchronous** — `CMD_SEND_*` returns a `tag`
immediately (`RESP_CODE_SENT`) and the answer arrives later as
`PUSH_CODE_BINARY_RESPONSE` with that `tag` — the read loop, not the send call, is
where replies are matched.

## 3. Node roles

MeshCore ships role-specific firmware. NightCrawler cares about the distinction
because it changes what it can ask:

- **Companion** — the client-attached node NightCrawler connects to. Also a valid
  crawl target if reachable as a contact.
- **Repeater** — headless infrastructure that re-floods packets. Holds the
  **neighbours table** and radio stats, answers the anonymous + guest requests.
  These are the primary crawl targets.
- **Room Server** — repeater-like plus a hosted message room. Same request
  surface for our purposes.
- **Sensor** — companion/repeater compiled with sensor support; serves telemetry.

Role is inferable from the advert (`ADV_TYPE_CHAT|REPEATER|ROOM|SENSOR`) and does
not require a query.

## 4. The two request surfaces NightCrawler uses

### 4a. Anonymous requests — `CMD_SEND_ANON_REQ` (57)

No login, no password, no session. The companion sends a `PAYLOAD_TYPE_ANON_REQ`
using an **ephemeral key** (so the crawler reveals no persistent identity) and,
for a non-contact target, auto-creates a transient `ADV_TYPE_NONE` contact
(zero-hop direct by default). The reply comes back as `PUSH_CODE_BINARY_RESPONSE`.

Three sub-types (`ANON_REQ_TYPE_*`, first byte of the request body):

| Sub-type | # | Returns (payload after the 4-byte tag) |
|---|---:|---|
| `ANON_REQ_TYPE_REGIONS` | 0x01 | `[node_clock:4][comma-separated flood-allowed region names]` — see §5. |
| `ANON_REQ_TYPE_OWNER` | 0x02 | `[node_clock:4]["<node_name>\n<owner_info>"]`. |
| `ANON_REQ_TYPE_BASIC` | 0x03 | `[node_clock:4][…feature bits]` — remote clock only (version intentionally excluded). |

Two hard constraints from the firmware, both of which shape the crawl:

1. **Direct route required.** `onAnonDataRecv` only honours `REGIONS`/`OWNER`/
   `BASIC` when the request arrives `isRouteDirect()` — i.e. the companion must
   already have a **path** to the target (zero-hop, or a path discovered via
   `CMD_GET_ADVERT_PATH` / `CMD_SEND_PATH_DISCOVERY_REQ`). A flooded anon request
   for these types is dropped. (Login, by contrast, is accepted on flood.)
2. **Node-side rate limiting.** The repeater runs an `anon_limiter` on these
   requests — originally documented as **max 4 every 3 minutes** per node
   (firmware commit `3af25495`). Exceed it and the node simply doesn't reply
   (returns 0). NightCrawler's own 1/min global limit sits well under this, but
   the crawler must not re-hammer a single node's anon endpoint on retry.

### 4b. Guest binary requests — `CMD_SEND_BINARY_REQ` (50), after `CMD_SEND_LOGIN`

After a successful guest login (§4c), the crawler is a known peer and can issue
structured `REQ_TYPE_*` requests. The reply again returns as
`PUSH_CODE_BINARY_RESPONSE` keyed by `tag`.

| `REQ_TYPE` | # | Returns | Guest? |
|---|---:|---|---|
| `REQ_TYPE_GET_STATUS` | 0x01 | `RepeaterStats` (batt mV, noise floor, last RSSI/SNR, packets recv/sent, flood/direct counts, dups, airtime, uptime, err flags). | ✅ "guests can also access this now" |
| `REQ_TYPE_GET_OWNER_INFO` | 0x07 | `"<FIRMWARE_VERSION>\n<node_name>\n<owner_info>"` — **the only remote source of the version string.** | ✅ |
| `REQ_TYPE_GET_NEIGHBOURS` | 0x06 | The neighbour table — see §6. | ✅ (no per-command admin check) |
| `REQ_TYPE_GET_TELEMETRY_DATA` | 0x03 | CayenneLPP; **guests get base telemetry only** (battery/temp) — `perm_mask` forced to 0. | ✅ (reduced) |
| `REQ_TYPE_GET_ACCESS_LIST` | 0x05 | ACL entries. | ❌ admin only |

### 4c. Login — `CMD_SEND_LOGIN` (26)

Sent as a `PAYLOAD_TYPE_ANON_REQ` whose body is the password string
(`handleLoginReq`). The node checks the string against the **admin** password
then the **guest** password; a match creates a client entry with the
corresponding permission. A **blank** password (`data[0]==0`) only logs in if the
sender is already in the node's ACL.

For NightCrawler v0.1:

- The crawler tries a **configured list of candidate guest passwords** for each
  node, in order, stopping at the first success (see
  [CONFIGURATION.md](CONFIGURATION.md)). The default list is **`["", "hello"]`** —
  empty and `hello` being the two conventions on the TwinOak / DK mesh.
- It **never** sends an admin password (it holds none), so it can never reach the
  admin tier — a structural guarantee that the crawler is read-only.
- `PUSH_CODE_LOGIN_SUCCESS` carries the permission byte and `FIRMWARE_VER_LEVEL`
  (a capability integer, currently 2 — *not* the version string). If a node were
  to answer with admin permissions, the crawler still treats the session as
  read-only and never escalates.

> **Guest login is cheap but not free.** It is a real handshake (one OTA
> round-trip) and it occupies a transient client slot on the node (guest entries
> are not persisted to flash, but they are live in the ACL for the session). This
> is why scopes/owner are read **anonymously** where possible — a node whose guest
> passwords we don't know still yields its identity and scopes.

## 5. Scopes / Regions — the primary objective

"Scope" = MeshCore's **region** tag on a packet. **Scoped** traffic is confined to
a named region; **un-scoped** traffic (`*`) floods the whole mesh. A node's
**default scope region** auto-tags its outbound flood traffic; congested meshes
(e.g. parts of DE) run `region denyf *` to stop un-scoped flooding entirely.

**What NightCrawler can actually read remotely — verified.** The anonymous
`ANON_REQ_TYPE_REGIONS` reply is produced by
`region_map.exportNamesTo(dest, len, REGION_DENY_FLOOD, invert=false)`. That
returns the **flood-*allowed*** region names — every region **without** the
`REGION_DENY_FLOOD` flag — and includes `*` if the wildcard/un-scoped region is
still flood-allowed. In other words, the reply is exactly:

> *"the set of scopes this node will re-flood, and whether it still floods
> un-scoped traffic (`*` present) or has locked down to named scopes (`*`
> absent)."*

This is precisely the signal the primary objective needs:

- **`*` present** → node still floods un-scoped; it has **not** adopted scoping.
- **`*` absent, e.g. `DK,DK-East`** → node only floods those named scopes;
  un-scoped flooding is disabled.
- **Comparing the flood-allowed sets of adjacent nodes** answers "do neighbouring
  nodes use similar scopes?" — a mismatch (one floods `*`, its neighbour only
  floods `DK`) is the operationally interesting finding.

**What is *not* remotely readable** (and therefore not in v0.1): the node's
*default scope region* name, the full parent/child region hierarchy, the
*denied* list, and `flood.max.unscoped`. Those live behind the **admin** `region`
CLI, which is serial-/admin-only. NightCrawler records what the anonymous reply
gives and nothing it cannot actually see.

> **Passive cross-check (future).** A node's *active* scope shows up as the
> transport code on the scoped adverts/flood packets it emits. If the companion
> surfaces transport codes on `PUSH_CODE_ADVERT`, the crawler could corroborate
> the queried flood-allowed set against observed on-air behaviour. Noted as a
> later enrichment, not v0.1 — see [ROADMAP.md](ROADMAP.md).

## 6. Reading a remote node's neighbour table (resolved)

This was the open "crux" of the first draft. It is now settled against source.

**Mechanism:** after guest login, send `CMD_SEND_BINARY_REQ` (50) with a body of
`[pubkey:32][REQ_TYPE_GET_NEIGHBOURS, request_version, count, offsetLo, offsetHi,
order_by, pubkey_prefix_length, rnd0..3]`:

- `request_version` = 0
- `count` = how many entries to return (0–255)
- `offset` = u16, for paging through a large table
- `order_by` = 0 newest→oldest · 1 oldest→newest · 2 strongest→weakest ·
  3 weakest→strongest
- `pubkey_prefix_length` = bytes of each neighbour's public key to return
  (clamped to full key size)

**Response** (`PUSH_CODE_BINARY_RESPONSE`, after `[reserved][tag:4]`):

```
[ total_neighbours : int16 ]      // how many the node knows in total
[ returned_count   : int16 ]      // how many are in this page
repeated returned_count times:
  [ pubkey_prefix : pubkey_prefix_length bytes ]
  [ heard_seconds_ago : uint32 ]
  [ snr : int8 ]                  // firmware stores snr*4; divide by 4 for dB
```

The node builds this table itself from **zero-hop adverts** it has heard
(`putNeighbour` on advert receipt), so it is the node's genuine RF-adjacency view.
Entries carry a **pubkey prefix, not always the full key** — the crawler stores
the prefix and merges it into the full node record when the full key is later
resolved (see [DATA-MODEL.md](DATA-MODEL.md) identity). Paging: request a page,
and if `returned_count < total_neighbours` request the next `offset`.

meshcore-cli / `meshcore_py` (`req_neighbours` / the binary-request verbs) are the
reference implementations to mirror for exact byte packing.

## 7. Rate & airtime context

The crawler's defaults exist because of physics and law, not caution for its own
sake:

- EU868 is duty-cycle limited; the **869.4–869.65 MHz** sub-band (which contains
  the network's 869.618 MHz home channel) permits up to **10%** duty cycle.
- MeshCore throttles its own TX to a configured duty cycle (`get/set dutycycle`
  from v1.15.0; the older `af` airtime-factor setting is deprecated).
- The observed **~6 messages/min** ceiling for the network is an **empirical
  capacity observation**, not a firmware constant. At SF8 / 62.5 kHz each message
  is hundreds of milliseconds of airtime; shared across infrastructure under a
  10% duty cycle, single-digit messages/min is the practical ceiling before
  congestion and loop-detection throttling bite.
- **Node-side anon limit:** as in §4a, a repeater answers at most ~**4 anon
  requests per 3 minutes**. The crawler's global 1/min pacing keeps it clear of
  this in aggregate, but it must also avoid bursting anon requests at one node.

NightCrawler therefore rate-limits **outbound over-the-air requests** and defaults
to a conservative **1/min**, well under the empirical ceiling. See
[CRAWL-ALGORITHM.md](CRAWL-ALGORITHM.md) for how the limiter is wired in.

## 8. No C#/.NET client exists

There is (as of this writing) **no C#/.NET MeshCore client library**. Existing
clients are Python (`meshcore` / `meshcore-cli`), JavaScript (`meshcore.js`) and
C99 (`SH3D/meshcore_c`). NightCrawler therefore implements the companion frame
protocol itself over a `TcpClient` — a small, self-contained slice, ported from
the Python/C references. If that slice grows useful beyond this tool, it can be
lifted into a standalone `MeshCore.Net` client package later
([ROADMAP](ROADMAP.md)).
