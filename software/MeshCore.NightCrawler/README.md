# MeshCore.NightCrawler

> A nightly mesh reconnaissance crawler for MeshCore networks.

NightCrawler is a small, single-purpose command-line utility that connects to a
MeshCore **companion** node over TCP/IP and, starting from that node's own
neighbours, walks the mesh hop by hop — querying every node it discovers for its
owner info, firmware version, configured scopes/regions and neighbour list — and
persists everything it learns to a simple JSON file on disk.

It is deliberately slow. The MeshCore RF band is congested and duty-cycle
limited, so the crawler is built around **throttling first**: it defaults to a
single request per minute and never floods the network to satisfy its own
curiosity. Run once a night, it slowly builds and refreshes a picture of the
mesh's topology and the state of the nodes in it.

This repository directory holds the **specification** for the project. It is the
first artefact — code comes after the design is settled.

---

## Why

The TwinOak MeshCore deployment is growing past the point where the topology
fits in one operator's head. Adverts tell you a node *exists*; they don't tell
you, in one place and over time:

- **Who owns it** (`owner.info`) and what it calls itself.
- **What firmware it runs** (`ver`) — so we can spot the nodes still on an old
  build before we push an OTA campaign from the fleet manager.
- **What scopes/regions it is configured for** — increasingly important as the
  Danish mesh (and neighbouring congested meshes) start disabling un-scoped
  flooding.
- **Who its neighbours are** — the actual RF adjacency graph, which is the thing
  that lets us reason about redundancy, essential hops, and where the next
  repeater should go.

The passive tooling in the ecosystem (the official MeshCore Map, MQTT observer
setups like MeshMapper/CoreScope) builds coverage from adverts and overheard
traffic. NightCrawler is the complementary **active** approach: log in where we
can, ask each node directly, and stitch the answers into a graph. As far as the
research for this spec could establish, no published tool currently does an
active, recursive neighbour-walk of MeshCore repeaters — so this starts as a
proof-of-concept to find out whether it is even viable at the network's duty
cycle.

## What it is (and is not)

**It is:**

- A **proof-of-concept** C# console application, kept as simple as it can be.
- A **read-mostly** crawler: it logs in to repeaters (to read neighbours and
  config) and issues query/`get` commands, but it does not reconfigure anything.
- A **polite** network citizen: hard rate limiting, a bounded crawl depth, and a
  visited-set so no node is ever queried twice in a run.
- A **producer of data** for the wider toolchain — its JSON output is designed to
  be ingested by [MeshCore.FleetManager](../MeshCore.FleetManager/) so that the
  crawl becomes an inventory-and-drift feed, not a throwaway report.

**It is not (yet):**

- Not a daemon or a service. v0 is "run it, it crawls, it writes a file, it
  exits." Scheduling is left to `cron`/Task Scheduler/the fleet manager.
- Not a configuration tool. It never `set`s anything on a node.
- Not a mapper/visualiser. It emits data; visualisation is somebody else's job.
- Not a general MeshCore client library. It implements only the slice of the
  companion protocol it needs. (A cleaner reusable client can be extracted later
  if it proves worth it — see [ROADMAP](ROADMAP.md).)

## Document map

| Document | What's in it |
|---|---|
| [REQUIREMENTS.md](REQUIREMENTS.md) | Functional and non-functional requirements, in priority order. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Component layout, the C# project structure, transport/connection design. |
| [CRAWL-ALGORITHM.md](CRAWL-ALGORITHM.md) | The traversal itself: frontier, visited-set, depth, throttle, per-node query sequence, failure handling. |
| [PROTOCOL.md](PROTOCOL.md) | The MeshCore companion + repeater-CLI surface NightCrawler depends on, with the caveats about what's confirmed vs. what must be verified against firmware. |
| [DATA-MODEL.md](DATA-MODEL.md) | The on-disk JSON schema for the node graph and crawl state. |
| [CONFIGURATION.md](CONFIGURATION.md) | Command-line options, config file, and defaults. |
| [ROADMAP.md](ROADMAP.md) | PoC scope, then where it could go. |

## Status

`spec / pre-implementation`. Nothing here is built yet. The immediate goal is a
PoC good enough to answer one question: **can we usefully crawl the mesh in a
single overnight window without saturating it?**

## Relationship to the rest of the repo

NightCrawler lives under `software/` in the TwinOak
`standard-repeater-design-v2` repo alongside the repeater hardware, the
[NoiseScope](../firmware/noisescope-rak4630/) scanner firmware, and the
[fleet manager](../MeshCore.FleetManager/). It shares the repo's MeshCore
conventions: **EU Narrow — 869.618 MHz, BW 62.5 kHz, SF 8, CR 8** is the home
channel of the network being crawled.

See [`software/software-goals.md`](../software-goals.md) for the broader
management-plane goals this feeds into.
