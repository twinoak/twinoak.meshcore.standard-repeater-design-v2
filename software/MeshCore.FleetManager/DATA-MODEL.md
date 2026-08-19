# FleetManager — Data model

The model is built around one central entity — the **Node** — with satellite
entities for the things that accrete over time (config backups, telemetry, RF
sweeps, firmware, events) and the things a node belongs to (sites, fleets).

Secrets are modelled **separately** from everything else and are the only
encrypted-at-rest, access-gated part of the store — see
[SECURITY.md](SECURITY.md). This separation is deliberate: the vast majority of
FleetManager data is ordinary operational data; only the key/password vault is
sensitive, and isolating it keeps the security surface small.

---

## 1. Entity overview

```
Site ──< Node >── Fleet(tag)
             │
             ├──< ConfigBackup >──(references)──> SecretBundle   [encrypted vault]
             ├──< TelemetrySample            (time-series)
             ├──< NoiseScopeSweep            (RF archive)
             ├──< FirmwareState / OtaCampaignRun
             ├──< NodeEvent                  (audit + lifecycle + control actions)
             └──  CapabilitySet              (embedded)

FirmwareImage (library, fleet-wide)
CrawlObservation (from NightCrawler, matched to Node)
```

## 2. `Node`

The system-of-record entity. Holds *intended* / authoritative state.

```jsonc
{
  "id": "uuid",
  "publicKey": "a1b2c3d4…",          // MeshCore identity; primary correlation key
  "label": "Grenaa Chimney #1",
  "role": "repeater",                 // repeater | roomserver | companion | sensor | observer
  "ownership": "owned",               // owned | third-party
  "tier": "lte-managed",              // derived from capabilities: lte-managed | mesh-only | third-party
  "lifecycle": "deployed",            // planned | provisioned | deployed | degraded | retired | lost

  "capabilities": {                   // FR-2 — what's actually possible, as flags
    "hasLteManagement": true,
    "canRemoteReboot": true,
    "canRemoteOta": true,
    "servesTelemetry": true,
    "canRunNoiseScope": true,
    "haveAdminCredentials": true,
    "reachableOverMesh": true
  },

  "hardware": {                       // the modular BOM per node
    "board": "RAK4630",
    "radio": "SX1262",
    "mgmtMcu": "Walter (ESP32-S3, LTE-M)",
    "enclosure": "diecast-alu IP67 + 2× G103 RF boxes",
    "filter": "CF866.5-KT30 cavity",
    "antenna": "Vinnant CC8688-PEL 10.14 dBi",
    "panel": "Cellevia CL-SM10P 10 W",
    "battery": "1S LiPo 10 Ah",
    "notes": "connector-board-a/b rev …, platform v1.1"
  },

  "radioConfig": {                    // intended radio params (the DK standard)
    "freqMHz": 869.618, "bwKHz": 62.5, "sf": 8, "cr": 8, "txDbm": 22
  },

  "network": {
    "defaultScopeRegion": "DK",
    "regions": ["DK"],
    "ownerInfo": "TwinOak — thomas@twinoak.dk"
  },

  "management": {                     // how to reach it (non-secret parts)
    "lte": {
      "provider": "NexCon",           // or Lebara
      "iccidRef": "secret:…",         // sensitive → vault reference, not inline
      "endpoint": "…",                // how the server reaches the Walter (see ARCHITECTURE)
      "lastContact": "2026-08-19T21:00:00+02:00"
    },
    "companionAccess": {              // if reachable as a companion/over mesh
      "host": null, "port": null
    }
  },

  "siteId": "uuid",
  "fleetTags": ["djursland", "essential-hop"],
  "thirdParty": {                     // present only when ownership = third-party
    "reasonInInventory": "only viable path to the Vejle cluster",
    "ownerContact": "…",
    "secretsHeld": false              // must be false for third-party
  },

  "currentFirmware": {                // observed/known, updated from telemetry/crawl/query
    "version": "v1.16.2", "source": "lte-query", "asOf": "…"
  },

  "createdAt": "…", "updatedAt": "…"
}
```

### Correlation

The **public key** is the join key across NightCrawler observations, MQTT
observer data, and inventory. `label` is for humans; never join on it. A node
whose key isn't known yet (planned but not provisioned) gets a placeholder and is
reconciled at provisioning.

## 3. `CapabilitySet`

Embedded in the node (shown above), but worth calling out as the model's hinge.
The UI and the API gate every management affordance on these flags. A mesh-only
node simply has the control capabilities `false`, so its detail page shows
inventory, backups and observed state but no reboot/OTA buttons. A third-party
node additionally has `haveAdminCredentials: false` and `secretsHeld: false`.
Capabilities can change (SIM dies → `hasLteManagement: false`) without changing
the node's identity.

## 4. `ConfigBackup` + `SecretBundle`

The config backup is split into a **non-secret snapshot** (safe, versioned,
freely readable) and a **secret bundle** (encrypted, access-gated). One backup
references at most one secret bundle.

```jsonc
// ConfigBackup — non-secret, versioned (FR-9, FR-11)
{
  "id": "uuid",
  "nodeId": "uuid",
  "version": 7,
  "capturedAt": "2026-08-19T20:00:00+02:00",
  "capturedVia": "lte-query",         // provisioning | lte-query | crawl | manual
  "config": {
    "name": "Grenaa Chimney #1",
    "ownerInfo": "…",
    "radio": { "freqMHz": 869.618, "bwKHz": 62.5, "sf": 8, "cr": 8, "txDbm": 22 },
    "regions": { "default": "DK", "list": ["DK"], "floodRules": {…}, "floodMaxUnscoped": 2 },
    "advert": { "zeroHopIntervalMin": 15, "floodIntervalHr": 6 },
    "repeat": true,
    "dutyCyclePct": 10,
    "customVars": {…}
  },
  "secretBundleRef": "vault:node/uuid/v7",   // pointer into the vault, not the secret
  "checksum": "sha256:…"
}

// SecretBundle — lives ONLY in the encrypted vault (SECURITY.md)
{
  "ref": "vault:node/uuid/v7",
  "privateKey": "…",                  // the node's MeshCore identity private key
  "adminPassword": "…",
  "guestPassword": "…",
  "lteSimIccid": "…",
  "notes": "…"
}
```

Restore (FR-13) reads a `ConfigBackup` + its `SecretBundle` and produces either a
CLI command script or a direct push (LTE nodes) to rebuild the node. Drift (FR-14)
diffs a `ConfigBackup.config` (non-secret part) against a `CrawlObservation` or a
fresh live query.

## 5. `TelemetrySample` (time-series)

High-volume, append-only. Likely a separate time-series-friendly store
([ARCHITECTURE](ARCHITECTURE.md)).

```jsonc
{
  "nodeId": "uuid",
  "ts": "2026-08-19T21:00:00+02:00",
  "source": "lte-poll",               // lte-poll | noisescope-heartbeat | mesh-telemetry | manual
  "power": {                          // INA3221 channels: panel / battery / load
    "vpanel": 18.10, "ipanel_mA": 120,
    "vbat": 4.012,  "ibat_mA": -52,
    "vload": 3.980, "iload_mA": 85
  },
  "environment": { "tempC": 21.4, "humidityPct": 55, "pressureHpa": 1013 },  // BME280
  "device": { "uptimeS": 864000, "resetReason": "por", "firmware": "v1.16.2" }
}
```

Derived series (state of charge, daily energy in/out, winter-survival projection —
FR-28) are computed from these.

## 6. `NoiseScopeSweep` (RF archive)

A permanently-kept, richly-contextualised RF measurement (FR-30, FR-31). The
**context is as important as the data** — a sweep is only comparable to another
if you know the antenna/filter/firmware were the same.

```jsonc
{
  "id": "uuid",
  "nodeId": "uuid",
  "siteId": "uuid",
  "capturedAt": "2026-08-19T23:30:00+02:00",
  "sessionType": "sweep",             // monitor | sweep | hist | dwell | cad
  "context": {                        // what the measurement was taken through
    "firmware": "noisescope 0.1.0",
    "radioConfig": { "freqMHz": 869.618, "bwKHz": 62.5, "sf": 8, "cr": 8 },
    "boostedGain": true,
    "antenna": "Vinnant CC8688-PEL 10.14 dBi",
    "filter": "CF866.5-KT30 cavity",
    "coax": "0.5 m H155",
    "notes": "post-filter-swap baseline"
  },
  "params": { "f0": 863.0, "f1": 870.0, "stepKhz": 25, "dwellMs": 20, "rbwKhz": 29.3 },
  "raw": [ /* verbatim NoiseScope NDJSON lines */ ],
  "summary": {                        // extracted for quick comparison/plots
    "noiseFloorDbm": -115.0, "maxHoldDbm": -99.5, "burstDutyCyclePct": 3.1
  }
}
```

Comparison (FR-31) matches two sweeps for the same site/node, aligns by
frequency, and renders old-vs-new (waterfall/spectrum + delta), guarding that the
`context` is compatible before claiming a meaningful delta.

## 7. Firmware entities

```jsonc
// FirmwareImage — fleet-wide library (FR-16)
{
  "id": "uuid",
  "product": "meshcore",              // meshcore | noisescope
  "version": "v1.16.2",
  "targetBoards": ["RAK4630", "Heltec V3"],
  "artifacts": { "hex": "…", "zip": "…", "elf": "…" },  // stored/referenced
  "checksum": "sha256:…",
  "provenance": "built from meshcore-dev@…, or upstream release",
  "releaseNotes": "…",
  "addedAt": "…"
}

// OtaCampaignRun — a firmware push to one node (FR-18, FR-20)
{
  "id": "uuid",
  "nodeId": "uuid",
  "fromVersion": "v1.15.1", "toImageId": "uuid",
  "startedAt": "…", "endedAt": "…",
  "state": "confirmed",               // staged | pushing | booting | health-check | confirmed | rolled-back | failed
  "healthGate": { "passed": true, "metrics": {…} },
  "log": "…"
}
```

## 8. `Site`

Physical location context, shared by nodes co-located there and reused from the
project's existing RF-site prospecting knowledge.

```jsonc
{
  "id": "uuid",
  "name": "Grenaa industrial chimney",
  "coordinates": { "lat": 56.41, "lon": 10.88 },
  "mount": "chimney bracket, ~40 m AGL",
  "access": "internal ladder; no harness climbing",
  "rfNotes": "cell tower adjacent — cavity filter mandatory; ~-110 dBm floor",
  "camo": "Montana BLACK / SprayMax 2K",
  "nodeIds": ["uuid", …]
}
```

## 9. `NodeEvent` (audit + lifecycle + control)

A single append-only event stream per node covers audit, lifecycle transitions
and control actions — one place to see everything that ever happened to a node.

```jsonc
{
  "id": "uuid",
  "nodeId": "uuid",
  "ts": "2026-08-19T21:05:00+02:00",
  "type": "control.reboot",           // lifecycle.* | config.backup | config.restore | control.reboot |
                                      // control.ota | secret.access | secret.rotate | drift.detected | note
  "actor": "thomas",                  // who/what initiated (user, api-client, schedule)
  "detail": { "reason": "unresponsive 4h", "result": "ok" },
  "correlatesTo": "uuid"              // e.g. the OtaCampaignRun or CrawlObservation involved
}
```

Every secret access and every control action **must** write a `NodeEvent`
([SECURITY](SECURITY.md), FR-39).

## 10. `CrawlObservation` (from NightCrawler)

The *observed* counterpart to the node's *intended* state — ingested from
NightCrawler's `mesh-graph.json` and matched by public key (FR-35).

```jsonc
{
  "nodeId": "uuid | null",            // null = observed but not in inventory (rogue candidate)
  "publicKey": "…",
  "observedAt": "…",
  "crawlRunId": "run-2026-08-19T2314",
  "observed": {                       // subset of NightCrawler's MeshNode
    "firmware": "v1.16.2",
    "ownerInfo": "…",
    "scopes": {…},
    "neighbours": [ {…} ],
    "role": "repeater",
    "reachedOverAir": true
  },
  "diffVsInventory": [                 // computed drift (FR-14, FR-37)
    { "field": "firmware", "intended": "v1.15.1", "observed": "v1.16.2" }
  ]
}
```

A `CrawlObservation` with `nodeId: null` is what surfaces in the "unknown/rogue
nodes" view (FR-36) — a prompt to adopt it (owned or third-party) or investigate.

## 11. Schema evolution

Per [NFR-7](REQUIREMENTS.md#9-non-functional-requirements), the model must absorb
new hardware, capabilities and telemetry channels without painful migrations.
Practical choices: capability and hardware blocks are open/extensible maps;
telemetry `power`/`environment` are open key spaces so a new sensor channel is
additive; the whole store carries a schema version; and the secret vault schema
is versioned independently of everything else so it can be hardened without
touching operational data.
