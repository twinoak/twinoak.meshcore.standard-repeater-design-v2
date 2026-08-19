# NightCrawler — Roadmap

The point of the PoC is to answer one question — *can we usefully crawl the mesh
overnight without saturating it?* — and to produce data good enough to feed the
fleet manager. Everything past that is contingent on the answer.

---

## Phase 0 — PoC (the only committed scope)

Deliver [everything tagged **[M]**](REQUIREMENTS.md) and stop:

- TCP companion connection + handshake.
- Companion frame protocol slice (just the frames in
  [PROTOCOL.md §2](PROTOCOL.md#2-frames-nightcrawler-uses)), opcodes verified
  against the firmware running on TwinOak repeaters.
- BFS traversal with visited-set, depth bound, and the token-bucket rate limiter
  as a hard choke point.
- Core per-node query tier: login, `ver`/`board`/`role`, `owner.info`,
  `neighbors`, region config.
- Incremental, atomic JSON persistence + run manifest.
- Console logging + end-of-run summary.

**Exit criterion:** one real overnight run against the live mesh at 1 msg/min,
depth 5, that (a) does not disrupt the network, (b) terminates cleanly, and (c)
produces a `mesh-graph.json` a human (and the fleet manager) can use. Write up
the observed nodes/night and any saturation effects — *that write-up is the PoC's
real deliverable.*

## Phase 1 — Make it dependable

Once the PoC proves viable:

- Transport auto-reconnect + resume-from-frontier (FR-3).
- `--continue` and `--refresh-older-than` multi-night modes (FR-30, §8 of the
  algorithm).
- Adaptive back-off on channel-busy / send failures (FR-27).
- Unit tests around the traversal against a faked mesh (NFR-7).
- Drift summary (`changes`) in the run manifest.

## Phase 2 — Feed the fleet

- A stable export/handoff to [MeshCore.FleetManager](../MeshCore.FleetManager/):
  either the fleet manager reads `mesh-graph.json` directly, or NightCrawler
  posts to a local ingest endpoint.
- Pull per-node login credentials from the fleet manager instead of env vars.
- Emit the fleet-manager-shaped drift feed (new/unknown nodes, firmware drift,
  scope drift, neighbour churn).

## Phase 3 — Extract a reusable client (only if it earns it)

If the companion-protocol slice proves solid and other tools want it, lift it out
of NightCrawler into a standalone **`MeshCore.Net`** client library (the missing
C#/.NET MeshCore client — there is none today per
[PROTOCOL §8](PROTOCOL.md#8-no-cnet-client-exists)). NightCrawler then becomes a
thin consumer of it. Do **not** do this pre-emptively; extract only once a second
consumer exists.

## Possible later directions (uncommitted)

- **Multi-vantage crawling** — run from several companions and merge, to see
  links no single vantage point hears (asymmetric-link coverage).
- **Extended-tier scheduling** — spend some nights on deep telemetry/stats of a
  subset rather than breadth.
- **Serial/BLE transports** (FR-4) for crawling from a directly-attached radio in
  the field.
- **Trace-based path mapping** — use `trace` per node to record actual routed
  paths and per-hop SNR, not just adjacency.
- **Anonymous-query experiments** — building on the RemoteTerm fork's anonymous
  repeater query endpoints (`req_regions_sync`, `req_owner_sync`) to reduce or
  remove the need to log in for read-only facts, cutting the request cost per
  node.

## Explicitly out of scope, indefinitely

- Writing/reconfiguring nodes (that's the fleet manager's job, deliberately kept
  separate so the crawler is provably read-only).
- Visualisation / mapping UI (emit data; let mappers map).
- Being a general-purpose MeshCore application.
