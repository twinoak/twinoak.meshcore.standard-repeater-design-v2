# FleetManager — Architecture

A single-operator server that is inventory-first, secrets-isolated, and
integration-heavy. The architecture optimises for **getting the durable
inventory + backup value early and low-risk**, while leaving clean seams for the
higher-risk control features to land later behind the
[security model](SECURITY.md).

---

## 1. High-level shape

```
                         ┌──────────────────────────────────────┐
                         │            FleetManager               │
                         │                                       │
   Web UI  ────────────► │  API (auth'd)                         │
   (operator)            │   ├─ Inventory & Sites                │
                         │   ├─ Config Backup / Restore ──────┐  │
   NightCrawler ───────► │   ├─ Ingest (crawl / telemetry)    │  │
   (mesh-graph.json)     │   ├─ Telemetry & Alerts            │  │
                         │   ├─ NoiseScope archive            │  │
   MQTT observers ─────► │   ├─ Firmware library & OTA        │  │
   (CoreScope/Mapper)    │   └─ Control (reboot/OTA/console)  │  │
                         │                                    ▼  │
                         │  Operational store   ┌── Secret Vault ┤ (encrypted,
                         │  (relational)        │   (isolated)   │  gated,
                         │  Time-series store   └────────────────┤  audited)
                         │  Blob/artefact store (sweeps, fw, logs)│
                         └───────────────┬───────────────────────┘
                                         │  authenticated, encrypted
                                         ▼  LTE/IP management plane
                          ┌──────────────────────────────┐
                          │  Walter LTE MCU  (per node)   │  ← the on-node agent
                          │  ↕ UART tunnel to MeshCore /  │
                          │    NoiseScope on the RAK4630  │
                          └──────────────────────────────┘
```

## 2. Components

### 2.1 API service (.NET)

The core. A .NET web API ([NFR-5](REQUIREMENTS.md#9-non-functional-requirements) —
Thomas's stack) exposing the capability areas above. Notable design points:

- **Capability-gated endpoints.** Control endpoints check the target node's
  [`CapabilitySet`](DATA-MODEL.md#3-capabilityset) — you cannot call reboot on a
  mesh-only node; the API returns "not capable," and the UI never shows the
  button. This keeps the tier logic in one place.
- **Secrets behind a dedicated, audited sub-service.** Nothing else in the API
  touches the vault directly; a `SecretService` mediates every read/write, writes
  the audit event, and enforces the operator gate ([SECURITY §4](SECURITY.md)).
- **Ingest endpoints** for NightCrawler and for node telemetry, separated from
  the interactive API so a firehose of samples doesn't compete with the UI.

### 2.2 Web UI

Single-operator web front-end (FR-40): inventory list + filters, per-node detail
(identity, hardware, capabilities, current vs intended config, telemetry charts,
NoiseScope archive, event/audit timeline), the drift dashboard, and the firmware/
OTA campaign view. Server-rendered or a thin SPA — whichever keeps it simple; this
is a tool, not a product. Reuse the project's existing visual language where it
helps.

### 2.3 Storage — three stores, by data shape

- **Operational store (relational).** Nodes, sites, capabilities, config-backup
  metadata, firmware library, OTA campaigns, events/audit, third-party records.
  PostgreSQL is the natural default; SQLite is a legitimate start for a
  single-operator box and eases the "runs on a Pi" goal
  ([NFR-3](REQUIREMENTS.md#9-non-functional-requirements)).
- **Time-series store.** Telemetry samples (FR-25) are append-only and grow
  without bound. Start pragmatic (a partitioned table in the relational store is
  fine at fleet scale), with a clean seam to move to a purpose-built TSDB
  (TimescaleDB/InfluxDB) if volume warrants.
- **Blob/artefact store.** NoiseScope raw NDJSON sweeps (FR-30), firmware images
  (FR-16), captured logs/crash dumps. Filesystem or object storage; referenced by
  metadata rows. These are large and cold — keep them out of the relational DB.
- **Secret vault.** Logically separate, encrypted, gated
  ([SECURITY](SECURITY.md)). Physically it can be a dedicated encrypted table/
  file or an external secrets manager; the point is isolation and independent
  hardening, not where the bytes sit.

### 2.4 The node-side agent (LTE plane)

FleetManager's remote-control features depend on a counterpart on the node — the
**Walter LTE MCU** running the management-plane logic from
[`software-goals.md`](../software-goals.md). FleetManager is the server; the
Walter is the agent. The division of responsibility:

| Concern | On the node (Walter/firmware) | In FleetManager (server) |
|---|---|---|
| A/B OTA + health-gated rollback | **Guarantees** un-brickability | Stages images, triggers, tracks campaign state |
| Watchdog + power-cycle | **Performs** it autonomously | Can *also* command a reboot; records it |
| Telemetry (power/env) | **Produces** it | Pulls on schedule, stores, trends, alerts |
| NoiseScope | **Runs** the scan, streams NDJSON | Triggers session, archives, compares |
| Remote console | **Tunnels** UART↔MeshCore CLI | Presents the console, authorises |
| Store-and-forward when LTE down | **Buffers** on node | Reconciles on reconnect |

This split matters: FleetManager never has to be the thing that keeps a node
alive (the node does that itself); it orchestrates and remembers.

## 3. The LTE management-plane connection

How the server talks to the nodes (FR-21–FR-24, [SECURITY §7](SECURITY.md)):

- **Bearer:** LTE-M via the node's SIM (NexCon or Lebara), IP to the server. Only
  *management* traffic — never mesh data — travels here, by project principle.
- **Pattern — recommended: broker / pull.** Nodes are behind carrier-grade NAT on
  cheap data plans; a node-initiated connection to a broker the server also talks
  to (MQTT over TLS, or a lightweight authenticated long-poll/websocket) avoids
  needing inbound reachability to each node. The node agent connects out,
  authenticates, and receives commands; telemetry flows the same way. This also
  naturally gives store-and-forward (the node queues while offline, drains on
  reconnect).
- **Alternative — direct.** If a node is directly reachable (static-ish endpoint),
  the server can connect to it. Modelled per-node in `management.lte.endpoint`.
- **Auth:** mutual application-layer authentication independent of the bearer;
  commands are authenticated so a node acts only on genuine instructions
  ([SECURITY §7](SECURITY.md)). The tiny NexCon data budget (25 MB/month) is
  ample for management + the ~15 kB/h a NoiseScope `auto 10` session costs, but
  the server should be frugal and prefer pull/heartbeat over chatty polling.

## 4. Ingest pipelines

Three sources feed the observed/state side:

1. **NightCrawler** (FR-35) — reads `mesh-graph.json` (or receives a POST),
   matches by public key, writes `CrawlObservation`s, computes drift, raises
   "unknown node" candidates.
2. **Node telemetry over LTE** (FR-26) — scheduled pulls / pushed heartbeats →
   `TelemetrySample`s; NoiseScope heartbeats carry power telemetry too.
3. **MQTT observers** (CoreScope/MeshMapper — [INTEGRATIONS](INTEGRATIONS.md)) —
   optional passive feed of adverts/overheard state to enrich observed data for
   nodes with no back-channel.

All three converge on the same node records keyed by public key, so the node
detail page shows one coherent picture from many vantage points.

## 5. Deployment

- **Target:** a single always-on host — home-lab box, small VPS, or the same
  Raspberry-Pi class already running the BBS. Containerised (one compose file:
  API + DB + reverse proxy) for reproducibility.
- **Runs frugally:** the workload is light (one fleet, periodic telemetry, nightly
  crawl ingest). The heavy/cold data (sweeps, firmware, logs) sits in blob
  storage, not RAM.
- **Backups:** operational store + blob store on a normal backup schedule; the
  vault backed up as ciphertext with separate master-key recovery
  ([SECURITY §8](SECURITY.md)).
- **Availability:** not HA in v1. If FleetManager is down, nodes keep running
  (they're autonomous); the server catches up on reconnect
  ([NFR-4](REQUIREMENTS.md#9-non-functional-requirements)).

## 6. Build order forcing-function

The architecture is drawn so that **inventory + config backup** stands entirely
on the operational store + vault + UI/API, with **zero** dependency on the LTE
control path. That subset is buildable and useful immediately and carries only the
"store secrets safely" risk — not the "reboot hardware remotely" risk. The
control/OTA components hang off clean seams (the node-agent connection, the
firmware library, capability gating) and land later, once both the security model
and the node-side agent are proven. See [ROADMAP](ROADMAP.md).
