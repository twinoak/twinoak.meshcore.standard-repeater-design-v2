# NightCrawler — MeshCore protocol surface

This document describes **only** the slice of MeshCore that NightCrawler depends
on, and — importantly — separates what is well-established from what must be
**verified against the firmware in the field** before it is hardcoded.

> ⚠️ **Trust boundary.** Command and response *names* below are stable across
> MeshCore versions. The numeric **opcodes drift between firmware builds**, and
> two official doc sources disagree on some values. Treat every integer here as
> "verify before you ship it." NightCrawler should read opcode constants from a
> single place (see [ARCHITECTURE.md](ARCHITECTURE.md)) so a firmware bump is a
> one-file change. The same caution applies to the **firmware version numbers**
> cited below (when Regions arrived, when `dutycycle` replaced `af`, etc.) — they
> are approximate and worth confirming against the build actually running on the
> TwinOak nodes.

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

1. `CMD_APP_START` → expect `RESP_CODE_SELF_INFO` (our companion's identity,
   name, public key, channel/radio params).
2. `CMD_DEVICE_QUERY` → `RESP_CODE_DEVICE_INFO` (firmware/version of *our*
   companion).
3. `CMD_GET_DEVICE_TIME` / `CMD_SET_DEVICE_TIME` as needed (clock sanity).
4. `CMD_GET_CONTACTS` → a stream of `RESP_CODE_CONTACT` between
   `RESP_CODE_CONTACTS_START` and `RESP_CODE_END_OF_CONTACTS` — the companion's
   known contacts (nodes it has heard adverts from). These become crawl
   seeds/enrichment (FR-7).

## 2. Frames NightCrawler uses

Names below are from the meshcore-dev Companion Radio Protocol wiki. Numeric
values are the wiki's decimal set and are **provisional**.

### App → firmware (`CMD_*`)

| Name | Purpose in NightCrawler |
|---|---|
| `CMD_APP_START` | Open the session. |
| `CMD_DEVICE_QUERY` | Read the companion's own device/version info. |
| `CMD_GET_CONTACTS` | Enumerate known contacts (seeds). |
| `CMD_GET_DEVICE_TIME` / `CMD_SET_DEVICE_TIME` | Clock sync. |
| `CMD_SEND_LOGIN` | Log in to a remote repeater/room server (admin or guest password). |
| `CMD_SEND_STATUS_REQ` | Request status from a node. |
| `CMD_GET_ADVERT_PATH` | Read the cached path to a contact (routing insight). |
| `CMD_SEND_TRACE_PATH` | Path trace (per-hop SNR) — optional enrichment. |
| `CMD_SEND_TELEMETRY_REQ` | Optional telemetry pull (FR-14). |
| *(repeater CLI passthrough)* | Send an arbitrary repeater CLI command to a logged-in node — the mechanism meshcore-cli exposes as `cmd "<command>"`. Used to read `neighbors`, `ver`, `get owner.info`, `region ...`. **Confirm the exact frame** (likely a raw/binary/control-data command) against firmware — see §5. |

### Firmware → app (`RESP_CODE_*` and `PUSH_CODE_*`)

| Name | Meaning |
|---|---|
| `RESP_CODE_OK` / `RESP_CODE_ERR` | Command ack; `ERR` carries a one-byte error code (`ERR_CODE_UNSUPPORTED_CMD`, `_NOT_FOUND`, `_TABLE_FULL`, `_BAD_STATE`, `_FILE_IO_ERROR`, `_ILLEGAL_ARG`). |
| `RESP_CODE_SELF_INFO` | Our companion's identity. |
| `RESP_CODE_DEVICE_INFO` | Device/version info. |
| `RESP_CODE_CONTACTS_START` / `RESP_CODE_CONTACT` / `RESP_CODE_END_OF_CONTACTS` | Contact enumeration stream. |
| `RESP_CODE_CURR_TIME` | Device clock. |
| `PUSH_CODE_LOGIN_SUCCESS` / `PUSH_CODE_LOGIN_FAIL` | Result of `CMD_SEND_LOGIN`. |
| `PUSH_CODE_STATUS_RESPONSE` | Status / **repeater CLI command output** comes back this way. |
| `PUSH_CODE_TELEMETRY_RESPONSE` | Telemetry payload (CayenneLPP — note: big-endian, the one exception to the little-endian rule). |
| `PUSH_CODE_TRACE_DATA` | Path-trace hops with SNR. |
| `PUSH_CODE_ADVERT` / `PUSH_CODE_NEW_ADVERT` | An advert arrived — a node announced itself. NightCrawler can opportunistically fold these into the graph while it runs. |
| `PUSH_CODE_PATH_UPDATED` | A path to a contact changed. |

Push codes have the high bit set (`0x80+`) and can arrive **at any time**,
unsolicited. The client's read loop must always be draining frames and routing
pushes independently of whatever command it is currently awaiting.

## 3. Node roles

MeshCore ships role-specific firmware. NightCrawler cares about the distinction
because it changes what it can ask and how:

- **Companion** — the client-attached node NightCrawler connects to. Also a valid
  crawl target if one is reachable as a contact.
- **Repeater** — headless infrastructure that re-floods packets. Holds the
  **neighbours table** and radio stats. Administered remotely via login + CLI.
  These are the primary crawl targets.
- **Room Server** — repeater-like plus a hosted message room. Same admin surface
  for our purposes.
- **Sensor** — companion/repeater compiled with sensor support; serves
  telemetry.

Role is discoverable via `get role` on a logged-in node, and often inferable
from the advert.

## 4. Repeater CLI surface

Once logged in (guest or admin), a client can issue the repeater's text CLI
commands and read the responses. The ones NightCrawler relies on (verbatim names
from the MeshCore Repeater & Room Server CLI reference):

| Command | NightCrawler use | Requirement |
|---|---|---|
| `ver` | Firmware version. | FR-9 |
| `board` | Hardware/board name. | FR-9 |
| `get role` | Node role. | FR-9 |
| `get owner.info` | Owner info string. | FR-8 |
| `get name` | Advertised name. | FR-12 |
| `get public.key` | Stable node identifier. | FR-12 |
| `neighbors` | The neighbour table — the ~8 most-recent directly-heard nodes with signal info. **The crawl's backbone.** | FR-6, FR-11 |
| `discover.neighbors` | Actively probe for zero-hop neighbours. **Costs airtime** — used sparingly / opt-in only. | FR-6 |
| `region list` / `region get` / `region default` | Configured scopes/regions and the node's default scope region. | FR-10 |
| `stats-radio` | Noise floor, RSSI/SNR, airtime, errors. | FR-13 |
| `clock` | Node's idea of UTC (clock-drift signal). | FR-13 |

Permission tiers gate these: `guest` / `read-only` / `read-write` / `admin`
(`setperm`). Reads NightCrawler needs (neighbours, owner info, region config)
generally require at least a successful login; some may require admin. **Which
reads need which tier must be confirmed per firmware version** (open question in
[REQUIREMENTS §4](REQUIREMENTS.md#4-open-questions-to-resolve-during-the-poc)).

## 5. Scopes / Regions

What Thomas calls "configured scopes" maps to MeshCore's **Regions** feature (~v1.10+,
expanded through v1.15/v1.16). A **scope** is a regional/routing tag on a packet:

- **Scoped** traffic is confined to a named region; **un-scoped** traffic floods
  the whole mesh.
- A node's **default scope region** (v1.15.0) auto-tags its outbound traffic to a
  chosen region — necessary in meshes that have disabled un-scoped flooding.
- `region allowf` / `region denyf` set per-region flood allow/deny (congested
  meshes run `region denyf *`).
- `flood.max.unscoped` caps hops for un-scoped packets separately.

For NightCrawler, "configured scopes" = the node's **region list, its default
scope region, and its allow/deny flood rules**, read via the `region ...`
sub-commands. The exact read-back sub-commands and output format are an
**open question to confirm against firmware**.

## 6. The remote-neighbour-read question (the crux)

The single most important thing to verify before implementation:

> **How, exactly, does a client behind a companion read a _remote_ repeater's
> neighbour table, and what does the response look like?**

The research points to two paths, and they need to be confirmed in the field:

1. **meshcore-cli `req_neighbours`** — a first-class verb, implying a dedicated
   request frame and structured response.
2. **`cmd "neighbors"` passthrough** — send the literal CLI string to a
   logged-in repeater and parse the `PUSH_CODE_STATUS_RESPONSE` text.

NightCrawler should prefer the structured request if it exists; otherwise it
parses the CLI text output. Either way this is the operation the whole crawl is
built on, so it gets nailed down first, against the actual firmware running on
TwinOak repeaters, before the traversal code is written. The Python
`meshcore_py` library and `meshcore-cli` are the reference implementations to
mirror; the C99 `SH3D/meshcore_c` client is a second reference for the raw frame
layer.

## 7. Rate & airtime context

The crawler's defaults exist because of physics and law, not caution for its own
sake:

- EU868 is duty-cycle limited; the **869.4–869.65 MHz** sub-band (which contains
  the network's 869.618 MHz home channel) permits up to **10%** duty cycle.
- MeshCore throttles its own TX to a configured duty cycle (`get/set dutycycle`
  from v1.15.0; the older `af` airtime-factor setting is deprecated).
- The observed **~6 messages/min** ceiling for the network is an **empirical
  capacity observation**, not a firmware constant. At the home channel's SF8 /
  62.5 kHz, each message is hundreds of milliseconds of airtime; shared across
  infrastructure under a 10% duty cycle, single-digit messages per minute is the
  practical ceiling before congestion and loop-detection throttling bite.

NightCrawler therefore rate-limits **outbound over-the-air requests** and
defaults to a conservative **1/min**, well under the empirical ceiling, leaving
the network's capacity for its actual users. See
[CRAWL-ALGORITHM.md](CRAWL-ALGORITHM.md) for how the limiter is wired in.

## 8. No C#/.NET client exists

There is (as of this writing) **no C#/.NET MeshCore client library**. Existing
clients are Python (`meshcore` / `meshcore-cli`), JavaScript (`meshcore.js`) and
C99 (`SH3D/meshcore_c`). NightCrawler therefore implements the companion frame
protocol itself over a `TcpClient` — a small, self-contained slice, ported from
the Python/C references. If that slice grows useful beyond this tool, it can be
lifted into a standalone `MeshCore.Net` client package later
([ROADMAP](ROADMAP.md)).
