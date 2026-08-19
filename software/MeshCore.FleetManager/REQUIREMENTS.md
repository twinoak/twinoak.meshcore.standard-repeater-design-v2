# FleetManager — Requirements

Priority tags: **[M]** must-have (core value), **[S]** should-have, **[C]** could-have / later.
Unlike NightCrawler, FleetManager is a long-lived system, so "must" here means
"must exist for the first genuinely useful version," which is defined as the
**inventory + config-backup vault** (see [ROADMAP](ROADMAP.md) phase 1). Remote
control is deliberately *not* in that first must-have set — it's gated behind the
security model.

---

## 1. Inventory & identity

- **[M] FR-1** Maintain a record for **every node in the fleet**, of any tier
  (LTE-managed, mesh-only, third-party), keyed on the MeshCore public key with a
  human-friendly label/name.
- **[M] FR-2** Record each node's **capability set** (what management actions are
  possible) rather than a rigid type, and derive the archetype (LTE-managed /
  mesh-only / third-party) from it.
- **[M] FR-3** Record **site/location** context: where it physically is (address /
  coordinates / "the chimney in Grenaa"), mount type, antenna, and site notes —
  reusing the RF-site knowledge the project already tracks.
- **[M] FR-4** Record **role** (repeater / room server / companion / sensor /
  observer) and **hardware** (board, radio module, enclosure, filter, panel,
  battery) — the modular BOM per node.
- **[S] FR-5** Track **third-party / foreign nodes** with why they're in
  inventory (e.g. "essential hop to reach Vejle"), owner contact if known, and
  explicitly **no secrets** held for them.
- **[S] FR-6** Group nodes into **logical fleets/regions** (e.g. Djursland,
  Vejle) and tag them freely.
- **[S] FR-7** Track **lifecycle state**: planned → provisioned → deployed →
  degraded → retired → lost, with dates and notes.
- **[C] FR-8** Track **relationships/dependencies** between nodes (this node's
  only viable path to the internet-map is via that third-party hop).

## 2. Configuration backup vault

- **[M] FR-9** Store a **restorable configuration backup** per owned node: all
  MeshCore settings needed to reconstruct the node — identity keys (**private
  key**), **admin/guest passwords**, name, owner info, radio params (freq/bw/sf/
  cr/tx), region/scope config, advert intervals, repeat settings, and any custom
  vars.
- **[M] FR-10** Store secrets (private keys, passwords) **encrypted at rest**,
  with access controlled and audited. This is a hard requirement — see
  [SECURITY.md](SECURITY.md).
- **[M] FR-11** Support **versioned** config backups: keep history so you can see
  what a node's config was at any past point and roll back to it.
- **[S] FR-12** **Capture** a backup automatically where possible — from a
  provisioning step, from a NightCrawler crawl (non-secret fields), or from a
  direct query over the LTE plane for LTE nodes.
- **[S] FR-13** **Restore / re-provision**: generate the exact CLI command
  sequence (or push it directly, for LTE nodes) to rebuild a replacement node
  with a lost node's identity and config.
- **[S] FR-14** Detect **config drift**: compare the stored intended config
  against what NightCrawler observed or what a live query returns, and flag
  differences.
- **[C] FR-15** **Key rotation** workflow: change an admin password or rotate an
  identity across the fleet and track which nodes are updated.

## 3. Firmware management (LTE tier)

- **[S] FR-16** Maintain a **firmware image library** (versions, target boards,
  build provenance, checksums, release notes) for MeshCore and for the
  [NoiseScope](../firmware/noisescope-rak4630/) diagnostic firmware.
- **[S] FR-17** Track **which firmware each node runs** (from telemetry / crawl /
  query) and flag nodes behind the current target — an "update campaign" view.
- **[C] FR-18** **Orchestrate remote OTA** over the LTE plane, honouring the
  on-node A/B + health-gated auto-rollback design: stage image → trigger →
  monitor health → confirm or auto-rollback. FleetManager tracks campaign state
  across the fleet; the node firmware guarantees un-brickability.
- **[C] FR-19** **Swap firmware** on an LTE node between MeshCore and NoiseScope
  for a diagnostic session and back, as the noisescope workflow describes
  (flash-in-place, scan, flash-back), preserving the node's config filesystem
  (application-area erase only — **never** chip erase).
- **[C] FR-20** Track **OTA history** and outcomes per node (success, rollback,
  failure) for reliability trending.

## 4. Remote state & control (LTE tier)

- **[S] FR-21** **Force reboot / power-cycle** an LTE node remotely (the Walter
  can power-kill the LoRa radio's 3V3 rail), with the action authorised and
  audited ([SECURITY](SECURITY.md)).
- **[S] FR-22** Read **live node state** on demand over LTE (up/down, current
  firmware, radio config, uptime, last reboot reason).
- **[C] FR-23** **Remote console passthrough** to a node's MeshCore CLI over the
  LTE UART tunnel — the "management from a desk" endgame.
- **[C] FR-24** **Scheduled / conditional actions** (e.g. auto power-cycle a node
  that's been unresponsive for N hours, matching the on-node watchdog with a
  fleet-level safety net).

## 5. Telemetry & environment archive

- **[M] FR-25** Store **time-series telemetry** per node: battery voltage/current
  and charge, panel voltage/current (INA3221 channels), and enclosure environment
  (BME280 temp/humidity/pressure) — the same channels the repeater hardware and
  NoiseScope heartbeats already expose.
- **[S] FR-26** **Automatically pull** power and environment stats from LTE nodes
  on a schedule (the "automatic pulling of statistics" requirement) and ingest
  the same fields from NoiseScope heartbeats and MeshCore telemetry frames.
- **[S] FR-27** **Trend and alert**: charts over time, and alerts on thresholds
  (battery low, not charging through a sunny day, temperature/condensation risk,
  node silent).
- **[S] FR-28** Correlate telemetry with the **darkest-winter power-budget**
  concern — flag nodes whose battery trend won't survive the winter stretch.
- **[C] FR-29** **Manual telemetry entry** for mesh-only nodes visited in person
  (a reading taken with a multimeter or read off the mesh).

## 6. NoiseScope RF archive

- **[M] FR-30** Store **NoiseScope sweeps/measurements** per node/site with full
  context (timestamp, firmware, radio config, antenna/filter setup, the raw
  NDJSON), so a sweep is a first-class, permanently-kept artefact.
- **[M] FR-31** Support **historical comparison**: put a sweep from last year
  next to one taken now for the same site and show the delta (the explicitly
  requested "1 sweep last year vs a sweep now" use case).
- **[S] FR-32** **Trigger a NoiseScope session** on an LTE node (flash → scan →
  collect → flash back) and file the results automatically (ties FR-19 + FR-30).
- **[S] FR-33** Render sweeps as **waterfalls/spectra** (reusing the
  `waterfall_plot.py` logic) and histograms, and annotate known blockers
  (LTE800/GSM-R bands) as the noisescope docs describe.
- **[C] FR-34** **Fleet-wide RF health** view: which sites are noisy, trending
  worse, or newly quiet after a filter change.

## 7. Observation & drift (the NightCrawler tie-in)

- **[M] FR-35** **Ingest NightCrawler's mesh graph** and match observed nodes to
  inventory by public key.
- **[S] FR-36** Surface **unknown/rogue nodes** the crawler found that aren't in
  inventory — candidates to adopt (as owned or third-party) or investigate.
- **[S] FR-37** Surface **drift**: firmware behind target, region/scope config
  changed, owner info changed, neighbours lost/gained, node gone silent.
- **[S] FR-38** Store the **neighbour graph over time** so topology change is
  visible (an essential hop that vanished, a new link that appeared).

## 8. Access, audit, operability

- **[M] FR-39** All access to secrets and all control actions are **authenticated
  and audited** (who/what/when) — see [SECURITY](SECURITY.md).
- **[S] FR-40** A **web UI** (single-operator) presenting inventory, a per-node
  detail page, telemetry charts, the NoiseScope archive, and the drift dashboard.
- **[S] FR-41** An **API** so NightCrawler, the Walter nodes, and future tools can
  push/pull without the UI.
- **[C] FR-42** **Backup/export** of the whole FleetManager datastore itself
  (it's the crown jewels — it must itself be backed up, encrypted).
- **[C] FR-43** **Map view** of the fleet (sites, links, health) — possibly by
  feeding the existing MeshMapper/CoreScope tooling rather than reimplementing.

---

## 9. Non-functional requirements

- **[M] NFR-1 Security-first.** This system holds private keys and admin
  passwords and can reboot hardware. Confidentiality of secrets and
  authorisation of control actions dominate every design decision
  ([SECURITY](SECURITY.md)).
- **[M] NFR-2 Durability.** Inventory, backups and the RF archive are long-lived
  records that must survive disk failure — the datastore is itself backed up, and
  the RF/telemetry archive is expected to grow for years.
- **[M] NFR-3 Single-operator simplicity, multi-user-ready.** Runs for one person
  managing one fleet, on modest hardware (a home-lab box / small VPS / the same
  Raspberry Pi class the BBS runs on), but the model shouldn't preclude adding
  users/roles later.
- **[S] NFR-4 Offline-tolerant.** LTE nodes go dark; the mesh is intermittent.
  The system degrades to "last known state" gracefully and reconciles when
  connectivity returns.
- **[S] NFR-5 Technology fit.** Thomas's stack is .NET/C# (LabelHub, NoiseScope
  tooling, NightCrawler). A .NET server + a straightforward relational store is
  the natural fit; time-series data may warrant a purpose-built store later.
- **[S] NFR-6 Auditability for external stakeholders.** The project already has a
  NIS 2 / utility-company angle; an auditable record of node config, access and
  changes is a latent asset there — design so it *could* satisfy that scrutiny.
- **[C] NFR-7 Extensible node model.** New hardware, new capabilities, new
  telemetry channels arrive constantly (the whole repeater project is built on
  modularity) — the schema must absorb them without migrations-from-hell.

---

## 10. Explicit non-goals

- Not sending management traffic over LoRa (LTE/IP is the management plane).
- Not re-implementing the mesh map — integrate with existing mappers instead.
- Not a public/multi-tenant service in v1.
- Not doing the on-node firmware's job (A/B, watchdog, health-gating live on the
  node; FleetManager orchestrates and records, it doesn't replace them).
