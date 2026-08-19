# FleetManager — Integrations

FleetManager is the hub the rest of the TwinOak toolchain plugs into. It rarely
originates data about the *outside* world — it correlates data from the crawler,
the nodes, the scanner and the ecosystem's observers into one authoritative
picture. This document maps each integration: what flows, which direction, and
the seam it uses.

---

## 1. NightCrawler → FleetManager (observed state & drift)

**Direction:** NightCrawler produces, FleetManager consumes.

- **What:** the nightly [`mesh-graph.json`](../MeshCore.NightCrawler/DATA-MODEL.md)
  — observed nodes, their firmware/owner/scopes, and the neighbour graph.
- **How:** FleetManager either reads the file directly (shared volume / path) or
  exposes an ingest endpoint NightCrawler POSTs to at run end. File-first is
  simplest for the PoC era.
- **Matching:** by **public key** → `CrawlObservation` per node
  ([data model](DATA-MODEL.md#10-crawlobservation-from-nightcrawler)).
- **Value produced:**
  - **Drift** (FR-37) — observed firmware/scope/owner vs the intended config
    backup; surfaced on the node page and the drift dashboard.
  - **Unknown/rogue nodes** (FR-36) — observations with no inventory match become
    "adopt or investigate" candidates (owned, third-party, or ignore).
  - **Topology over time** (FR-38) — neighbour graphs archived per run so a lost
    essential hop or a new link is visible.
- **Reverse flow (later):** FleetManager can *serve NightCrawler its login
  credentials* from the vault over a local authenticated channel, so the crawler
  stops needing env-var passwords
  ([NightCrawler CONFIGURATION §3](../MeshCore.NightCrawler/CONFIGURATION.md#3-secrets--passwords)).
  This is the clean long-term division: FleetManager owns secrets, NightCrawler
  borrows them per run.

## 2. NoiseScope ↔ FleetManager (RF archive & sessions)

**Direction:** both.

- **Archive (NoiseScope → FleetManager, FR-30/31):** the scanner's NDJSON output
  (`boot`/`stat`/`sweep`/`hist`/`dwell`/`cad` lines, per its
  [PROTOCOL](../firmware/noisescope-rak4630/PROTOCOL.md)) is stored verbatim as a
  `NoiseScopeSweep` with full context (firmware, radio config, antenna, filter,
  coax). The context capture is what makes a **last-year-vs-now** comparison
  (FR-31) trustworthy — you can prove the two sweeps saw the world through the
  same front end.
- **Sessions (FleetManager → node, FR-32):** for LTE nodes, FleetManager can
  orchestrate a full diagnostic session: flash NoiseScope in place of MeshCore
  (application-area erase only — **never** chip erase, preserving identity/config
  per the noisescope docs), let it stream sweeps to the Walter, collect + archive,
  then flash MeshCore back. The Walter forwards NDJSON (it needs *no* understanding
  of the payload — the format is designed for dumb forwarding).
- **Rendering (FR-33):** reuse the `tools/waterfall_plot.py` logic to render
  waterfalls/spectra/histograms, annotating known blockers (LTE800 DL 791–821,
  GSM-R/GSM900 DL 921–960) as the noisescope README describes.
- **Power double-duty:** NoiseScope heartbeats carry the B-board INA3221 power
  telemetry, so a scanning session also yields `TelemetrySample`s — one trip, two
  datasets.

## 3. Walter LTE MCU ↔ FleetManager (the management plane)

**Direction:** both, over LTE/IP ([ARCHITECTURE §3](ARCHITECTURE.md#3-the-lte-management-plane-connection)).

The Walter is FleetManager's on-node agent. The integration surface:

- **Telemetry push / pull** (FR-25/26) — power (panel/battery/load via INA3221)
  and environment (BME280) on a schedule or as heartbeats.
- **Control commands** (FR-21) — force reboot / power-cycle (the Walter can kill
  the LoRa 3V3 rail), authenticated per [SECURITY §7](SECURITY.md).
- **OTA orchestration** (FR-18) — FleetManager stages a firmware image; the Walter
  performs the A/B flash of the RAK4630 over SWD and reports health; FleetManager
  tracks the campaign and confirms/records the auto-rollback outcome. The node
  guarantees un-brickability; the server remembers what happened.
- **Remote console** (FR-23) — the Walter tunnels UART↔MeshCore CLI; FleetManager
  presents the console and authorises access.
- **LTE self-monitoring & store-and-forward** — the Walter buffers when the SIM is
  offline and drains on reconnect; FleetManager reconciles gaps.
- **Config capture/restore over LTE** (FR-12/13) — for LTE nodes, FleetManager can
  query the live config (via the console tunnel) to capture a backup, or push a
  restore, without a site visit.

## 4. MeshCore ecosystem observers (optional enrichment)

**Direction:** ecosystem → FleetManager (read-only enrichment).

The project already runs an **MQTT observer** node (Heltec V3 in Grindsted)
bridging to MeshMapper.net and meshview.dk (**CoreScope** for Denmark). That feed
can enrich FleetManager's *observed* view for nodes with no back-channel:

- Subscribe to the observer's MQTT topics for adverts / overheard node state and
  fold them into node records (last-heard, position, role) keyed by public key.
- Reuse **CoreScope**'s health/usefulness/bridge scoring as an external signal on
  a node's importance, rather than reinventing it.
- **Do not reinvent the mesh map** (FR-43 / non-goal) — where a visual map is
  wanted, feed the existing MeshMapper/CoreScope tooling FleetManager's inventory
  (labels, ownership, site context) so the community map and the private fleet
  view reinforce each other.

## 5. Firmware sources (library provenance)

**Direction:** external → FleetManager.

The [`FirmwareImage`](DATA-MODEL.md#7-firmware-entities) library tracks both
MeshCore builds (from `meshcore-dev/MeshCore` releases or Thomas's own builds) and
the NoiseScope firmware (built in-repo via PlatformIO). Integration is mostly
metadata discipline: record version, target boards, checksums, and provenance
(upstream release vs local build vs a PR branch), so an OTA campaign always knows
exactly what it's shipping and can refuse anything unverified
([SECURITY §5](SECURITY.md)).

## 6. Integration summary

| Integration | Dir | Flows | Key seam |
|---|---|---|---|
| NightCrawler | in (later: out) | observed nodes, neighbour graph, drift; (later) credentials out | `mesh-graph.json` / ingest API; vault read for creds |
| NoiseScope | both | RF sweeps in; session trigger + flash orchestration out | NDJSON archive; LTE flash-in-place workflow |
| Walter/LTE | both | telemetry, control, OTA, console, config capture/restore | authenticated LTE broker/pull channel |
| MQTT observers (CoreScope/MeshMapper) | in | adverts/overheard state, scoring | MQTT subscribe, match by public key |
| Firmware sources | in | images + provenance | firmware library metadata |

Everything converges on the **public key** as the correlation key and on the
**node record** as the single place the operator looks — which is the whole point
of FleetManager existing.
