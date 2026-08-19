# NightCrawler — Architecture

The guiding constraint is [NFR-1: simplicity](REQUIREMENTS.md#2-non-functional-requirements).
This is a PoC console app. The architecture below is the *smallest* structure
that keeps the three genuinely tricky concerns — the frame protocol, the
throttle, and the traversal — cleanly separable and testable, and nothing more.

---

## 1. Layers

```
┌─────────────────────────────────────────────────────────────┐
│  CLI / host  (Program.cs)                                     │
│  parse args + config, build the object graph, run, report     │
└───────────────┬───────────────────────────────────────────────┘
                │
┌───────────────▼───────────────────────────────────────────────┐
│  Crawler  (the traversal)                                      │
│  frontier · visited-set · depth bound · per-node query plan    │
│  — pure logic, no sockets, no clock it doesn't own —           │
└───────┬───────────────────────────────────┬───────────────────┘
        │ asks for data                      │ every OTA request goes through
┌───────▼─────────────────┐        ┌─────────▼───────────────────┐
│  IMeshClient            │        │  IRateLimiter               │
│  high-level ops:        │        │  the single choke point     │
│  GetSelf, GetContacts,  │        │  (msgs/min token bucket)    │
│  Login, GetNeighbours,  │        └─────────────────────────────┘
│  GetOwnerInfo, GetVer,  │
│  GetRegions, GetStats   │
└───────┬─────────────────┘
        │ implemented by
┌───────▼─────────────────────────────────────────────────────┐
│  MeshCoreCompanionClient                                       │
│  companion frame protocol over a transport                    │
│  ├─ FrameCodec      (encode/decode <…>, >…len… payloads)      │
│  ├─ OpCodes         (the single place opcodes live)           │
│  ├─ read loop       (drains frames, routes PUSH_* asynchronously)│
│  └─ ITransport      (TcpTransport now; SerialTransport later) │
└───────┬───────────────────────────────────────────────────────┘
        │
┌───────▼───────────────┐        ┌─────────────────────────────┐
│  ITransport → TCP      │        │  IGraphStore                │
│  (TcpClient stream)    │        │  load/save the JSON graph   │
└────────────────────────┘        │  atomic incremental writes  │
                                   └─────────────────────────────┘
```

### Why these seams

- **Crawler ↔ IMeshClient.** The traversal is the part with the interesting
  bugs (loops, double-queries, depth off-by-ones). Putting a high-level
  `IMeshClient` interface between it and the wire means the whole traversal can
  be unit-tested against a fake client with a scripted mesh — no radio, no
  sockets, deterministic ([NFR-7](REQUIREMENTS.md#2-non-functional-requirements)).
- **IRateLimiter as a hard choke point.** [FR-24](REQUIREMENTS.md#16-throttling-first-class-not-an-afterthought)
  says it must be *structurally impossible* to skip the throttle. So the limiter
  is not a politeness call sprinkled around the code — every `IMeshClient` method
  that emits an over-the-air request `await`s a token from the limiter before it
  sends, inside the client implementation. The crawler can't forget to call it
  because the crawler never calls it directly.
- **OpCodes in one file.** Per [PROTOCOL.md](PROTOCOL.md) the numeric opcodes are
  not fully trustworthy across firmware versions. Isolating them means a firmware
  bump is a one-file edit.

## 2. Concurrency model

**Single logical worker.** At 1 message/minute there is nothing to parallelise —
the rate limiter is the bottleneck by a factor of thousands, not the CPU or the
socket. The crawl is a simple `async` loop: pull the next node from the frontier,
run its query plan (each step `await`s a rate token), fold the results into the
graph, enqueue new neighbours, persist, repeat.

The **only** concurrency is the client's background **read loop**, which must run
continuously to drain inbound frames and dispatch unsolicited `PUSH_*` frames
(adverts, path updates) regardless of what command is currently outstanding. This
is modelled as one long-running `async` read task feeding an internal channel;
command methods await the specific response frame they expect, pushes are routed
to handlers. (Per Thomas's stated preference, this is plain linear `async`/`await`
with a `Channel<T>`/`await foreach`, not hand-rolled `TaskCompletionSource`
plumbing or `Promise`-style constructs.)

## 3. Suggested project layout

A single solution, one runnable project, one test project:

```
MeshCore.NightCrawler/
├─ MeshCore.NightCrawler.sln
├─ src/
│  └─ MeshCore.NightCrawler/
│     ├─ Program.cs                 // arg/config parse, wire-up, run, summary
│     ├─ CrawlOptions.cs            // strongly-typed config (depth, rate, host…)
│     ├─ Crawl/
│     │  ├─ Crawler.cs              // the traversal (frontier, visited, depth)
│     │  ├─ Frontier.cs             // queue of (nodeId, depth) to visit
│     │  ├─ NodeQueryPlan.cs        // the per-node sequence of asks
│     │  └─ CrawlSummary.cs         // end-of-run stats
│     ├─ Mesh/
│     │  ├─ IMeshClient.cs          // high-level ops (the crawler's view)
│     │  ├─ MeshCoreCompanionClient.cs
│     │  ├─ FrameCodec.cs           // <…> / >…len… framing
│     │  ├─ OpCodes.cs              // CMD_/RESP_/PUSH_ constants (per firmware)
│     │  ├─ CompanionReadLoop.cs    // background frame drain + push routing
│     │  └─ Transports/
│     │     ├─ ITransport.cs
│     │     └─ TcpTransport.cs      // (SerialTransport.cs later — FR-4)
│     ├─ RateLimiting/
│     │  └─ TokenBucketRateLimiter.cs   // IRateLimiter, msgs/min
│     ├─ Model/
│     │  ├─ MeshNode.cs             // one node record (see DATA-MODEL.md)
│     │  ├─ Neighbour.cs
│     │  ├─ MeshGraph.cs            // the whole graph + run manifest
│     │  └─ RunManifest.cs
│     └─ Storage/
│        ├─ IGraphStore.cs
│        └─ JsonGraphStore.cs       // atomic incremental persistence
└─ tests/
   └─ MeshCore.NightCrawler.Tests/
      ├─ CrawlerTests.cs            // loops, depth, visited-set, budgets
      ├─ RateLimiterTests.cs
      ├─ FrameCodecTests.cs
      └─ FakeMeshClient.cs         // scripted mesh for traversal tests
```

If even this feels heavy for a first cut, it collapses cleanly to a handful of
files in one project; the folders are the *conceptual* seams, and the interfaces
(`IMeshClient`, `IRateLimiter`, `ITransport`, `IGraphStore`) are the ones worth
keeping no matter how small the implementation starts.

## 4. Dependencies

Keep it lean:

- **System.Text.Json** for persistence (in-box, source-generatable, fast). No
  Newtonsoft.
- A minimal arg parser — either hand-rolled or **System.CommandLine** if the flag
  surface justifies it. Config file binding via
  `Microsoft.Extensions.Configuration` is optional; a plain JSON `appsettings`
  read is fine for the PoC.
- **No** database, ORM, message bus, or web framework.
- Logging: `Microsoft.Extensions.Logging` with the console provider, or plain
  `Console` writes behind a tiny wrapper. Nothing heavier.

## 5. Configuration flow

`Program.cs` builds a `CrawlOptions` from, in ascending precedence: built-in
defaults → optional `appsettings.json` → environment variables → command-line
flags. The safe defaults (1 msg/min, depth 5) live in code so that *no*
configuration is required to run safely. See
[CONFIGURATION.md](CONFIGURATION.md).

## 6. Error & lifecycle handling

- **Per-node failures** (timeout, auth fail, malformed response) are caught,
  recorded on the node's record, and never abort the run
  ([NFR-5](REQUIREMENTS.md#2-non-functional-requirements)).
- **Transport drop** → the client raises a reconnect; the crawler pauses, the
  client re-establishes the TCP session and re-does the handshake, and the crawl
  resumes from the persisted frontier (FR-3). The visited-set and graph are
  already on disk, so at worst one node's partial query is retried.
- **Cancellation** — Ctrl-C / `SIGTERM` triggers a graceful stop: finish the
  current node (or abandon it), flush the graph and manifest, print the summary,
  exit non-zero to signal "incomplete." An overnight run stopped by the time
  budget (FR-22) does the same but exits zero.
- **Atomic writes** — the graph is written to a temp file and `File.Move`d over
  the target so a crash mid-write never corrupts the JSON
  ([FR-29](REQUIREMENTS.md#17-persistence)).
