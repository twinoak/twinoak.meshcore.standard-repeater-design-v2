# NightCrawler — Requirements

Priority tags: **[M]** must-have for the PoC, **[S]** should-have, **[C]** could-have / later.
The PoC is defined as *everything tagged [M], and nothing else is required to
call the PoC done.*

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

For each node the crawler reaches, it attempts to collect:

- **[M] FR-8** **Owner info** (`owner.info`).
- **[M] FR-9** **Firmware version** (`ver`) and, where available, board/hardware
  name (`board`) and role (companion / repeater / room server / sensor).
- **[M] FR-10** **Configured scopes / regions** — the node's default scope region
  and its region allow/deny flood configuration, to whatever depth the node
  exposes to a logged-in admin.
- **[M] FR-11** **Neighbour list** with, where the firmware provides it, per-
  neighbour signal quality (RSSI/SNR) and last-heard time.
- **[S] FR-12** **Public key / node ID** and advertised name for every node, so
  records are stably keyed even before a full query succeeds.
- **[S] FR-13** **Basic radio/link stats** (`stats-radio`: noise floor, airtime,
  error counts) for repeaters we can log in to.
- **[C] FR-14** **Telemetry** (battery, environment) where a node serves it —
  useful but overlaps with the fleet manager's job, so it stays optional here.
- **[C] FR-15** **Position** (lat/lon) if advertised, for later mapping.

### 1.4 Authentication

- **[M] FR-16** Support logging in to repeaters/room servers with an **admin or
  guest password** so that admin-gated reads (neighbours, owner info, region
  config) succeed. Passwords are supplied by configuration, and the crawler
  supports a **default/shared password** plus **per-node overrides**.
- **[M] FR-17** Degrade gracefully when login fails or no password is known:
  record whatever is obtainable anonymously (name, public key, presence,
  advertised data) and mark the node as `auth-failed` / `partial`.
- **[S] FR-18** Never store passwords in the output data file. (They come from
  config; the output records only *whether* auth succeeded.)

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

## 4. Open questions to resolve during the PoC

These are the things the PoC exists to answer or that need a firmware check
before coding the relevant part:

1. **Remote neighbour read.** Confirm the exact mechanism and command to read a
   *remote* repeater's neighbour table via a logged-in companion session
   (`req_neighbours` / `cmd "neighbors"` passthrough) and the response shape.
   See [PROTOCOL.md §6](PROTOCOL.md#6-the-remote-neighbour-read-question-the-crux).
2. **Frame opcodes.** Pin the numeric `CMD_*` / `RESP_CODE_*` / `PUSH_CODE_*`
   values against the firmware version in the field before hardcoding them.
3. **Scope/region read-back.** Confirm which `region ...` sub-commands a
   logged-in admin can *read* remotely, and their output format.
4. **Viability.** With depth 5 at 1 msg/min, how many nodes can realistically be
   covered in one night, and does that saturate anything? This is the core PoC
   result.
