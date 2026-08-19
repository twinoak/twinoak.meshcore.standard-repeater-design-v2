# NightCrawler — Data model & JSON persistence

The output is a single JSON file describing the mesh as NightCrawler has come to
know it, plus a per-run manifest. It is designed to be (a) trivially diffable
between nights, (b) crash-safe to write incrementally, and (c) ingestible by
[MeshCore.FleetManager](../MeshCore.FleetManager/).

---

## 1. Design principles

- **One file, one graph.** Default `mesh-graph.json`. Nodes are a map keyed by
  stable node ID so lookups and merges are O(1) and diffs are line-local.
- **Accretive, not destructive.** A new run updates fields and advances
  timestamps; it does not drop nodes it didn't reach this time. Disappearance is
  represented by a stale `lastSeen`, not by deletion.
- **Field-level provenance.** Every non-trivial fact carries when it was learned
  and how, so the fleet manager can trust-rank and age data.
- **Schema-versioned.** A top-level `schemaVersion` lets consumers evolve safely
  ([NFR-8](REQUIREMENTS.md#2-non-functional-requirements)).
- **No secrets, ever.** Passwords used for login live in config, never in this
  file. Records store only *whether* auth succeeded and at what permission tier.

## 2. Identity

Nodes are keyed on the MeshCore **public key** (the stable node identifier),
rendered as a lowercase hex string. Until the real key is known, a node
referenced only as a neighbour is held under a **provisional key** (a prefix or a
synthesised `pending:<hint>` id) and merged into the real record once a query
reads its public key. The model therefore carries both `publicKey` and a set of
`aliasKeys`/prefixes to support that merge.

## 3. Top-level document

```jsonc
{
  "schemaVersion": 1,
  "network": {
    "name": "TwinOak / MeshCore Denmark",
    "homeChannel": { "freqMHz": 869.618, "bwKHz": 62.5, "sf": 8, "cr": 8 }
  },
  "companion": {
    "publicKey": "…",           // the vantage-point node this graph was crawled from
    "name": "…",
    "host": "…", "port": 5000
  },
  "generatedAt": "2026-08-19T23:14:00+02:00",
  "lastRun": "run-2026-08-19T2314",
  "nodes": {
    "<publicKey>": { /* MeshNode — see §4 */ }
  },
  "edges": [ /* adjacency — see §5 */ ],
  "runs": [ /* RunManifest history — see §6 */ ]
}
```

## 4. `MeshNode`

```jsonc
{
  "publicKey": "a1b2c3d4…",
  "aliasKeys": ["a1b2c3"],              // provisional prefixes merged into this node
  "name": "TwinOak-Grenaa-Chimney",
  "role": "repeater",                    // companion | repeater | roomserver | sensor | unknown
  "board": "RAK4630",                    // hardware name, best-effort (advert/heuristic)
  "firmware": {
    "version": "v1.17.1",                // only present if a guest login succeeded
    "raw": "…full ver string…"           // from REQ_TYPE_GET_OWNER_INFO
  },
  "ownerInfo": "TwinOak — thomas@twinoak.dk",  // from ANON_REQ_TYPE_OWNER (anonymous)
  "position": { "lat": 56.41, "lon": 10.88, "source": "advert" },  // if advertised

  "scopes": {                            // MeshCore "regions" — FR-10, the primary datum
    // From the anonymous ANON_REQ_TYPE_REGIONS reply: the node's FLOOD-ALLOWED
    // scope names. This is all that is remotely readable (default-scope name,
    // denied list and hierarchy are admin/serial-only — see PROTOCOL §5).
    "floodAllowedRegions": ["DK", "DK-East"],  // named scopes this node re-floods
    "floodsUnscoped": false,             // true iff "*" was present in the reply
    "raw": "DK,DK-East"                  // verbatim comma-separated reply
  },

  "neighbours": [ /* Neighbour — see §4.1 */ ],

  "radioStats": {                        // optional, extended tier — FR-13
    "noiseFloorDbm": -115.0,
    "airtimePct": 3.1,
    "rxErrors": 0,
    "asOf": "2026-08-19T23:20:00+02:00"
  },
  "telemetry": {                         // optional — FR-14
    "batteryV": 4.01, "batteryPct": 92, "charging": true,
    "environment": { "tempC": 21.4, "humidityPct": 55, "pressureHpa": 1013 },
    "asOf": "…"
  },

  "access": {
    "anonReadOk": true,                  // scopes/owner read anonymously
    "guestLoginAttempted": true,
    "guestLoginSucceeded": true,
    "guestPasswordIndex": 1,             // which candidate matched (index, NOT the password)
    "permissionTier": "guest",           // guest | none  (admin is never attempted in v0.1)
    "reachedOverAir": true               // false = known only from advert/contact
  },

  "discovery": {
    "depth": 2,                          // shallowest hop distance from the seed
    "discoveredVia": ["b7f0…","advert"], // node(s)/mechanism that referenced it
    "status": "crawled"                  // crawled | scope-only | guest-auth-failed | partial | referenced | unreachable | beyond-depth
  },

  "timestamps": {
    "firstSeen": "2026-08-12T23:40:00+02:00",
    "lastSeen": "2026-08-19T23:20:00+02:00",   // any evidence (advert or query)
    "lastCrawlAttempt": "2026-08-19T23:18:00+02:00",
    "lastCrawled": "2026-08-19T23:20:00+02:00"  // last successful data pull
  },

  "provenance": {                        // per-field "as-of" + source request
    "scopes":           { "asOf": "…", "source": "anon-regions" },     // anonymous
    "ownerInfo":        { "asOf": "…", "source": "anon-owner" },       // anonymous
    "firmware.version": { "asOf": "…", "source": "get-owner-info" },   // guest
    "neighbours":       { "asOf": "…", "source": "get-neighbours" }    // guest
  },

  "errors": [                            // field-level failures, non-fatal
    { "field": "neighbours", "asOf": "…", "message": "guest login failed (no candidate password matched)" }
  ]
}
```

### 4.1 `Neighbour`

An entry in a node's neighbour table, as returned by `REQ_TYPE_GET_NEIGHBOURS`.
The response carries a pubkey prefix, SNR and how long ago the neighbour was
heard — there is **no per-neighbour RSSI** in this table.

```jsonc
{
  "publicKey": "c9ae…",       // usually a prefix (pubkey_prefix_length bytes)
  "name": "…",                // if resolvable from the graph
  "snrDb": 7.5,               // firmware sends snr*4; store the divided value
  "lastHeard": "2026-08-19T23:05:00+02:00",  // derived from heard_seconds_ago
  "hops": 0                    // neighbour tables are built from zero-hop adverts
}
```

## 5. `edges`

The adjacency list, kept separately from node records so the graph can be loaded
into mapping/analysis tools directly. Edges are recorded even when the far node
is past the depth bound (FR-19).

```jsonc
{
  "from": "a1b2c3d4…",
  "to":   "c9ae…",
  "snrDb": 7.5,
  "directed": true,            // neighbour tables are directional (who *I* hear)
  "asOf": "2026-08-19T23:05:00+02:00",
  "observedVia": "get-neighbours@a1b2c3d4…",
  "scopeMatch": "same"         // same | differ | unknown — do both ends share a flood-allowed scope? (the primary comparison)
}
```

Directionality matters: A hearing B does not imply B hears A (asymmetric links
are common and diagnostically interesting). Where both directions are observed,
two edges exist and a consumer can infer a bidirectional link.

`scopeMatch` is the payoff of the primary objective made queryable at the edge
level: for each adjacency, do the two ends share at least one flood-allowed
scope? `differ` edges — a node and its neighbour that will not re-flood each
other's traffic — are exactly the silent holes the crawl exists to surface. It is
`unknown` when either end's scope set hasn't been read yet.

## 6. `RunManifest`

One per crawl, appended to `runs`, so nightly history and trends are visible
without an external log store (FR-31).

```jsonc
{
  "runId": "run-2026-08-19T2314",
  "startedAt": "2026-08-19T23:14:00+02:00",
  "endedAt":   "2026-08-20T07:46:00+02:00",   // ~8.5 h: 512 requests at 1/min is throttle-bound
  "reason": "frontier-empty",       // frontier-empty | max-depth | max-nodes | deadline | cancelled | transport-lost
  "config": {
    "maxDepth": 5, "ratePerMin": 1, "maxNodes": null, "deadline": "2026-08-20T06:00:00+02:00",
    "queryTier": "core"
  },
  "counters": {
    "requestsSent": 512,
    "nodesQueried": 104,
    "scopesMapped": 104,             // nodes whose flood-allowed scope set was read (the primary KPI)
    "fullyCrawled": 98,              // nodes that also yielded neighbours + version (guest login worked)
    "newNodes": 3,
    "refreshed": 101,
    "guestAuthFailures": 6,          // nodes that rejected all candidate guest passwords (still scope-mapped)
    "unreachable": 4,
    "throttleWaitSeconds": 30720
  },
  "changes": [                       // optional drift summary vs previous run
    { "node": "a1b2…", "field": "scopes.floodsUnscoped", "from": true, "to": false },
    { "node": "e4d1…", "field": "firmware.version", "from": "v1.15.1", "to": "v1.17.1" },
    { "node": "c9ae…", "field": "neighbours", "note": "lost 2, gained 1" }
  ]
}
```

The `changes` block is the seed of the drift-detection the fleet manager builds
on: a per-night, human-readable "what moved."

## 7. Persistence mechanics

- **Atomic writes.** Serialise to `mesh-graph.json.tmp`, then `File.Move` over
  `mesh-graph.json`. A crash never leaves a half-written file
  ([FR-29](REQUIREMENTS.md#17-persistence)).
- **Incremental cadence.** The graph is saved after each node completes, not only
  at run end (FR-29). For a large graph, a future optimisation is to write only
  changed nodes / use a journal, but for PoC scale (hundreds of nodes) a full
  rewrite per node is fine and simplest.
- **Human-readable.** Pretty-printed, stable key ordering (nodes sorted by key,
  fields in a fixed order) so `git diff` between nights is meaningful. The file
  is small enough to commit or archive per night if desired.
- **Load-merge on start.** On startup the previous file is loaded; the run
  updates it in place (FR-30). Missing file = fresh graph.

## 8. Consumption by the fleet manager

The fleet manager ([INTEGRATIONS](../MeshCore.FleetManager/INTEGRATIONS.md))
ingests this file as a **discovery/drift feed**: it matches crawled nodes to its
inventory by public key, flags nodes the crawler found that aren't in inventory
(rogue/unknown/third-party hops), surfaces firmware and scope drift, and stores
neighbour graphs over time. NightCrawler's schema is intentionally a superset of
what a single query returns and a subset of what the fleet manager stores — it is
the *observed* view, the fleet manager holds the *intended* view, and the diff
between them is the operationally interesting part.
