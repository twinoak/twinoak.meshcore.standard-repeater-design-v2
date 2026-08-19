# MeshCore.NightCrawler

> A nightly mesh reconnaissance crawler for MeshCore networks.

NightCrawler is a small, single-purpose command-line utility that connects to a
MeshCore **companion** node over TCP/IP and, starting from that node's own
neighbours, walks the mesh hop by hop, building a picture of every node it can
reach and persisting it to a simple JSON file on disk.

**Its primary objective is to map the use of scopes (MeshCore "regions") across
the network** — which nodes still flood un-scoped, which have locked down to
named scopes, and, crucially, **whether neighbouring nodes are configured for
similar scopes or not**. Owner info, firmware version and the neighbour graph are
collected alongside as the supporting context that makes the scope map
actionable.

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

- **What scopes/regions it is configured to flood** — *the primary question.* As
  the Danish mesh (and neighbouring congested meshes) start disabling un-scoped
  flooding, whether a node still floods `*` or only floods named scopes decides
  whether it actually carries traffic for its neighbours. A repeater on a
  different scope than the nodes around it is a silent hole in the mesh, and
  today there is no way to see that at a glance. NightCrawler exists to surface
  it: read each node's flood-allowed scope set and compare adjacent nodes.
- **Who owns it** (`owner.info`) and what it calls itself.
- **What firmware it runs** — so we can spot the nodes still on an old build
  before we push an OTA campaign from the fleet manager.
- **Who its neighbours are** — the actual RF adjacency graph, which both anchors
  the scope comparison ("are these two nodes *actually* neighbours?") and lets us
  reason about redundancy, essential hops, and where the next repeater should go.

The passive tooling in the ecosystem (the official MeshCore Map, MQTT observer
setups like MeshMapper/CoreScope) builds coverage from adverts and overheard
traffic — but a node's *configured scope set* is not in its adverts, so passive
tooling can't see it. NightCrawler is the complementary **active** approach: ask
each node directly (anonymously where it can, with a guest login where it must)
and stitch the answers into a graph. As far as the research for this spec could
establish, no published tool currently does an active, recursive scope-and-
neighbour walk of MeshCore repeaters — so this starts as a proof-of-concept to
find out whether it is even viable at the network's duty cycle.

## What it is (and is not)

**It is:**

- A **proof-of-concept** C# console application, kept as simple as it can be.
- A **read-only** crawler with **no admin access**: it reads scopes and owner
  info **anonymously**, and where it needs neighbours/firmware it performs a
  **guest login only** (trying a configured list of candidate guest passwords —
  by default empty and `hello`). It never holds or sends an admin password and
  never reconfigures anything. See [PROTOCOL §0](PROTOCOL.md#0-the-one-thing-that-shaped-v01-what-a-node-answers-and-to-whom).
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
| [PROTOCOL.md](PROTOCOL.md) | The MeshCore companion + repeater request surface NightCrawler depends on — opcodes and request tiers read directly from the v1.17.1 firmware source, including exactly what is answerable anonymously vs. only with a guest login. |
| [DATA-MODEL.md](DATA-MODEL.md) | The on-disk JSON schema for the node graph and crawl state. |
| [CONFIGURATION.md](CONFIGURATION.md) | Command-line options, config file, and defaults. |
| [ROADMAP.md](ROADMAP.md) | PoC scope, then where it could go. |

## Status

`spec / pre-implementation`. Nothing here is built yet. The immediate goal is a
PoC good enough to answer one question: **can we usefully map the network's scope
usage (and neighbour graph) in a single overnight window without saturating the
mesh?**

## Relationship to the rest of the repo

NightCrawler lives under `software/` in the TwinOak
`standard-repeater-design-v2` repo alongside the repeater hardware, the
[NoiseScope](../firmware/noisescope-rak4630/) scanner firmware, and the
[fleet manager](../MeshCore.FleetManager/). It shares the repo's MeshCore
conventions: **EU Narrow — 869.618 MHz, BW 62.5 kHz, SF 8, CR 8** is the home
channel of the network being crawled.

See [`software/software-goals.md`](../software-goals.md) for the broader
management-plane goals this feeds into.
