# NightCrawler — Requirements

Priority tags: **[M]** must-have for the PoC, **[S]** should-have, **[C]** could-have / later.
The PoC is defined as *everything tagged [M], and nothing else is required to
call the PoC done.*

> **Primary objective.** NightCrawler exists first and foremost to **map scope
> (region) usage across the network and reveal whether neighbouring nodes use
> similar scopes** (FR-10 + FR-11). Every other datum it collects is supporting
> context. When a trade-off arises, the scope map wins.

> **Auth model (v0.1).** The crawler operates at the **anonymous** and **guest**
> tiers only — never admin, never a text CLI command. Scopes and owner info are
> read anonymously; neighbours, firmware version and stats need a **guest login**,
> which the crawler attempts with a configured list of candidate guest passwords.
> The full tier breakdown is in [PROTOCOL §0](PROTOCOL.md#0-the-one-thing-that-shaped-v01-what-a-node-answers-and-to-whom).

---

## 1. Functional requirements

### 1.1 Connection

- **[M] FR-1** Connect to a single MeshCore **companion** node over **TCP/IP**
  (the WiFi-companion transport). Host and port are configurable; port defaults
  to **5000** (the meshcore-cli convention).
- **[M] FR-2** Perform the companion start-up handshake (app-start → read
  self-info) and confirm the companion is alive and on the expected channel
  before crawling.
- **[S] FR-3** Reconnect automatically if the TCP link drops mid-crawl, resuming
  from the persisted frontier rather than restarting.
- **[C] FR-4** Optionally support the serial/USB transport as well, behind the
  same interface. (Not needed for the PoC — the companion is on the network.)

### 1.2 Discovery / seeding

- **[M] FR-5** Take the companion's **own neighbours** as the seed set (hop 0 →
  its direct neighbours are hop 1).
- **[M] FR-6** For every discovered node, attempt to retrieve its **neighbour
  list**, and enqueue any not-yet-seen neighbours for crawling.
- **[S] FR-7** Also fold in nodes the companion already knows as **contacts**
  (from received adverts) as additional seeds/enrichment, so the crawl isn't
  blind to nodes it has heard but not yet reached.

### 1.3 Per-node data collection

For each node the crawler reaches, it attempts to collect (in priority order —
scopes first, since that is the point):

- **[M] FR-10** **Configured scopes / regions — the primary datum.** Read the
  node's **flood-allowed scope set** via the anonymous `ANON_REQ_TYPE_REGIONS`
  request: the list of region names it will re-flood, and whether `*` (un-scoped
  flooding) is still among them. This is obtainable with **no login**. The
  crawler records this set verbatim and derives a `floodsUnscoped` flag from it.
  (The default-scope-region name, denied list and full region hierarchy are
  *not* remotely readable — they are admin/serial-only — so they are out of scope
  for v0.1; see [PROTOCOL §5](PROTOCOL.md#5-scopes--regions--the-primary-objective).)
- **[M] FR-11** **Neighbour list** with per-neighbour signal quality (SNR) and
  last-heard time, via the **guest** `REQ_TYPE_GET_NEIGHBOURS` request. This both
  drives the crawl (discovers the next hop) and provides the adjacency needed to
  answer "do these two *neighbours* use similar scopes?" Requires a guest login;
  if login fails, the node's own neighbours are unavailable (its scope is still
  recorded anonymously).
- **[M] FR-8** **Owner info** — node name + `owner.info`, via the anonymous
  `ANON_REQ_TYPE_OWNER` request (**no login**). When a guest login succeeds this
  also arrives, together with the version, from `REQ_TYPE_GET_OWNER_INFO`.
- **[M] FR-9** **Firmware version** and role. The full version string is **only
  available with a guest login** (`REQ_TYPE_GET_OWNER_INFO`); it was deliberately
  removed from the anonymous responses. Role (companion / repeater / room server /
  sensor) is inferable from the advert without any query. Board/hardware name is
  best-effort (not separately exposed to guests in v1.17.1).
- **[S] FR-12** **Public key / node ID** and advertised name for every node, so
  records are stably keyed even before a full query succeeds.
- **[S] FR-13** **Basic radio/link stats** — noise floor, RSSI/SNR, packet
  counts, airtime, error counts — via the **guest** `REQ_TYPE_GET_STATUS`
  request (`RepeaterStats`), for repeaters we can guest-log in to.
- **[C] FR-14** **Telemetry** (battery, environment) where a node serves it —
  useful but overlaps with the fleet manager's job, so it stays optional here.
- **[C] FR-15** **Position** (lat/lon) if advertised, for later mapping.

### 1.4 Authentication (guest-only)

- **[M] FR-16** Log in to repeaters/room servers at the **guest tier only**, to
  unlock the guest reads (neighbours, firmware version, status). The crawler
  tries a **configured, ordered list of candidate guest passwords** per node and
  stops at the first success; the default list is **`["", "hello"]`** (empty and
  `hello`). Per-node overrides are supported. The crawler holds **no admin
  password and never sends one** — reaching the admin tier is structurally
  impossible, which is what makes "read-only" a guarantee rather than a promise.
- **[M] FR-17** Degrade gracefully when no candidate password works: the node's
  **scope set and owner info are still recorded** (both are anonymous), and the
  node is marked `guest-auth-failed` / `partial` — its neighbours and firmware
  version are simply left unknown. **Losing a guest login never loses the primary
  datum**, because scopes don't need one.
- **[S] FR-18** Never store passwords in the output data file. (Guest passwords
  are low-sensitivity — often blank or `hello` — but the output still records
  only *whether* guest login succeeded and at what tier, never the password.)

### 1.5 Traversal control

- **[M] FR-19** Enforce a configurable **maximum crawl depth** (hop count from
  the seed), defaulting to **5**. Nodes beyond the depth limit may still be
  *recorded* as neighbours-of-neighbours but are not themselves queried.
- **[M] FR-20** Maintain a **visited set** so no node is queried more than once
  per run, keyed on the node's stable identifier (public key / node ID).
- **[S] FR-21** Support an optional **maximum node budget** (stop after N nodes
  queried) as a second safety bound independent of depth.
- **[S] FR-22** Support an optional **overall time budget** (e.g. "stop after
  6 hours") so an overnight run is guaranteed to finish before morning.

### 1.6 Throttling (first-class, not an afterthought)

- **[M] FR-23** Enforce a global **maximum messages-per-minute** rate limit
  across the entire crawl, defaulting to **1 message/minute**. The limit is on
  *outbound requests injected into the mesh*, aggregated over all in-flight
  nodes.
- **[M] FR-24** The rate limiter must be the single choke point through which
  every mesh-bound request passes — it is structurally impossible to bypass it.
- **[S] FR-25** Make the rate configurable up to a stated ceiling (the network is
  observed to tolerate ~6 msg/min) and warn loudly if the operator sets it
  higher.
- **[S] FR-26** Distinguish requests that touch the **companion only** (local,
  no airtime — reading contacts, self-info) from requests that **go over the
  air** (querying a remote node). Only the latter are rate-limited.
- **[C] FR-27** Adaptive back-off: if the companion reports the channel is busy
  or messages are failing/timing out, automatically slow down further.

### 1.7 Persistence

- **[M] FR-28** Persist the discovered node graph to a **JSON file** on disk (see
  [DATA-MODEL.md](DATA-MODEL.md)).
- **[M] FR-29** Persist **incrementally / crash-safely** — the file is written
  (atomically) as the crawl progresses, not only at the end, so an interrupted
  overnight run still yields usable data.
- **[M] FR-30** On a subsequent run, **load the previous file** and update it in
  place: refresh nodes that were re-queried, keep nodes that weren't, and record
  first-seen / last-seen / last-crawled timestamps per node.
- **[S] FR-31** Keep a lightweight **run log / manifest** (per run: start/end
  time, nodes queried, requests sent, failures) so crawls are auditable and
  trends across nights are visible.
- **[C] FR-32** Optional export to a second format (CSV, or a JSON shaped for the
  fleet manager's ingest endpoint).

### 1.8 Output & operability

- **[M] FR-33** Log progress to the console at a sensible level (node reached,
  query result, rate-limit waits) with a `--verbose` switch for frame-level
  detail.
- **[S] FR-34** Print a concise end-of-run summary: nodes known, nodes queried
  this run, new nodes, unreachable nodes, requests spent, wall-clock time.
- **[C] FR-35** A `--dry-run` that plans the crawl (shows the seed set and
  estimated request budget) without sending anything over the air.

---

## 2. Non-functional requirements

- **[M] NFR-1 Simplicity.** This is a PoC. Prefer the smallest thing that works:
  a single console project, minimal dependencies, no database, no DI container
  unless it genuinely earns its place. Readability over cleverness.
- **[M] NFR-2 Language/runtime.** C# on a current .NET LTS (**.NET 8**),
  cross-platform (it must run on the operator's Windows box and equally on a
  Linux host/Raspberry Pi that could run it on a schedule).
- **[M] NFR-3 Network safety.** The default configuration must be safe to point
  at the live production mesh without special thought: 1 msg/min, depth 5. Doing
  nothing dangerous by default is a hard requirement, not a nicety.
- **[M] NFR-4 Determinism of the visited-set.** The crawler must never loop, must
  never re-query a node, and must terminate — even in a mesh full of cycles
  (which every real mesh is).
- **[S] NFR-5 Resilience.** A single unreachable or misbehaving node must not
  abort the crawl; timeouts and errors are recorded and the crawl moves on.
- **[S] NFR-6 Observability.** Enough logging to answer "what did it do last
  night and why did it take that long" without a debugger.
- **[S] NFR-7 Testability.** The crawl logic (frontier, visited-set, depth,
  throttle) should be unit-testable against a faked MeshCore client, with no
  radio in the loop.
- **[C] NFR-8 Portability of output.** The JSON schema is versioned so the fleet
  manager and future tools can evolve alongside it without silent breakage.

---

## 3. Explicit non-goals for the PoC

- No writing/reconfiguring nodes.
- No GUI, no web UI, no live map.
- No multi-companion / multi-vantage-point crawling (one companion, one run).
- No BLE transport.
- No real-time streaming; a nightly batch is the whole model.
- No attempt to be a general MeshCore library.

---

## 4. Open questions

The protocol questions that dominated the first draft are now **resolved against
the v1.17.1 firmware source** and captured in [PROTOCOL.md](PROTOCOL.md):

- ~~Remote neighbour read mechanism~~ → **resolved:** `REQ_TYPE_GET_NEIGHBOURS`
  via `CMD_SEND_BINARY_REQ` after a guest login; response shape in
  [PROTOCOL §6](PROTOCOL.md#6-reading-a-remote-nodes-neighbour-table-resolved).
- ~~Frame opcodes~~ → **resolved** for v1.17.1 (still pinned to the field
  firmware via `CMD_DEVICE_QUERY`; kept in one `OpCodes` file).
- ~~Scope/region read-back~~ → **resolved:** the anonymous `ANON_REQ_TYPE_REGIONS`
  reply is the node's flood-allowed scope set; nothing deeper is remotely
  readable ([PROTOCOL §5](PROTOCOL.md#5-scopes--regions--the-primary-objective)).

What genuinely remains for the PoC to answer:

1. **Guest coverage.** On the live mesh, how many repeaters actually accept a
   blank or `hello` guest login? Nodes that reject both still yield scopes+owner
   anonymously — but their neighbour lists (and hence deeper discovery through
   them) are unavailable. The real per-night neighbour-graph coverage depends on
   this, and it is not knowable without running.
2. **Path-discovery cost.** Anonymous scope/owner requests require a **direct
   route** to the target. For nodes the companion has no cached path to, a
   path-discovery adds an OTA request before the query. How often that is needed
   (vs. reusing cached advert paths) directly affects the request budget.
3. **Passive scope corroboration.** Does the companion surface the transport
   code / scope on `PUSH_CODE_ADVERT`, so a node's *active* scope can be
   cross-checked against its *configured* flood-allowed set without a query? If
   so, it is a cheap enrichment (see [PROTOCOL §5](PROTOCOL.md#5-scopes--regions--the-primary-objective)).
4. **Viability.** With depth 5 at 1 msg/min, how many nodes can realistically be
   scope-mapped in one night, and does that saturate anything? This is the core
   PoC result.
