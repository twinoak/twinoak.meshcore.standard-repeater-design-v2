# FleetManager — Roadmap

The build order is chosen so that **durable value lands first and risk lands
last**. Inventory and config backup are useful the day they exist and only carry
"store secrets safely" risk. Remote control (reboot, OTA over LTE) is deferred
until both the security model and the node-side agent are proven — a bug there
disrupts real hardware on chimneys.

Each phase is independently valuable; you can stop after any of them and still
have a better system than a spreadsheet.

---

## Phase 1 — Inventory + config-backup vault (the must-have)

Delivers [FR-1–FR-11, FR-25, FR-30–FR-31, FR-35, FR-39](REQUIREMENTS.md) —
everything that stands on the operational store + vault + UI, with no LTE control
path.

- Node inventory across all tiers (LTE-managed, mesh-only, third-party), keyed by
  public key, with capability sets, hardware BOM, sites, lifecycle state.
- The **config-backup vault**: versioned non-secret snapshots + the isolated,
  encrypted, audited secret bundle ([SECURITY](SECURITY.md)). Manual capture to
  start (paste/import a node's config + keys).
- **NoiseScope archive**: store sweeps with context; last-year-vs-now comparison
  view (FR-31) — high value, zero control risk, uses data you're already
  producing.
- **Telemetry storage + charts** (manual/imported to start).
- **NightCrawler ingest**: match observations to inventory, show drift and
  unknown-node candidates (FR-35/36/37) — turns the crawler's output into
  standing value.
- Web UI: inventory list, node detail, NoiseScope archive, drift dashboard.
- Audit log from day one (FR-39).

**Exit criterion:** every node Thomas owns is in the system with a restorable,
encrypted config backup; NightCrawler's nightly graph lands and shows drift; last
year's sweeps sit next to this year's. No node has been rebooted by the server —
and that's fine.

## Phase 2 — Automatic telemetry + the LTE read path

The first LTE-plane integration, but **read-only** — lower risk than control.

- Node-agent connection to the Walter over the LTE broker/pull channel
  ([ARCHITECTURE §3](ARCHITECTURE.md#3-the-lte-management-plane-connection)),
  authenticated, telemetry-only.
- **Automatic pulling of power + environment stats** (FR-26) on a schedule;
  store-and-forward reconciliation.
- Trending + alerts (FR-27): battery low, not charging on a sunny day,
  condensation-risk temperature swings, node silent, **winter-survival
  projection** (FR-28).
- Live *read* of node state over LTE (FR-22) — up/down, firmware, config, uptime,
  reset reason — feeding drift detection with a second, authoritative source
  alongside the crawl.
- Config **capture** over LTE (FR-12) — snapshot a live node's config into the
  vault without a site visit.

## Phase 3 — Remote control (the high-stakes part)

Only after Phase 1's security model and Phase 2's authenticated channel are
proven in the field.

- **Force reboot / power-cycle** over LTE (FR-21), fully authorised + audited,
  with confirmation and reversibility guarantees ([SECURITY §5](SECURITY.md)).
- **Config restore / re-provision** (FR-13): rebuild a replacement node with a
  lost node's identity from the vault.
- **Scheduled/conditional actions** (FR-24): a fleet-level safety net matching the
  on-node watchdog (auto power-cycle a node unresponsive for N hours), bounded and
  audited.
- **Remote console passthrough** (FR-23) to the MeshCore CLI over the UART tunnel.

## Phase 4 — Firmware management & OTA campaigns

The most dependent on the node-side A/B + health-gating being rock-solid.

- **Firmware image library** with provenance + checksums (FR-16), for MeshCore
  and NoiseScope.
- **"Who's behind" campaign view** (FR-17): which nodes lag the target build.
- **Orchestrated OTA over LTE** (FR-18): stage → trigger → monitor health →
  confirm/auto-rollback, tracked per node, refusing unverified images.
- **NoiseScope-session orchestration** (FR-19/32): flash-in-place → scan →
  archive → flash-back, from the desk, results filed automatically.
- OTA history + reliability trending (FR-20).

## Phase 5 — Polish, scale & external-facing

- Fleet **map view** (FR-43) by feeding MeshMapper/CoreScope rather than
  reimplementing.
- **Key rotation** workflows across the fleet (FR-15).
- **Multi-user / roles** if it's ever needed (single-operator until then).
- **Auditability hardening** toward the NIS 2 / utility-company angle
  ([SECURITY §9](SECURITY.md)) — should the external interest firm up, the audit
  trail is already there to build on.
- Datastore self-backup/export tooling (FR-42), including tested vault restores.

---

## Guiding principles across all phases

- **Read before write, observe before control.** Every phase adds authority
  gradually: first we *know*, then we *watch*, then we *act*, then we *update*.
- **The node stays autonomous.** FleetManager never becomes load-bearing for a
  node's survival — the firmware's watchdog, A/B and health-gating do that. The
  server orchestrates and remembers.
- **Secrets isolated from day one.** The vault seam exists in Phase 1 even though
  control comes later, so secrets are never retrofitted into a system that grew up
  without them.
- **Stop-anywhere value.** Each phase leaves a system worth running on its own.
