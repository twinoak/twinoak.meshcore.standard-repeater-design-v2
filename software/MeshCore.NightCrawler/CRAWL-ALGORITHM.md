# NightCrawler — The crawl algorithm

This is the heart of the tool. Everything else exists to serve a correct,
terminating, polite traversal of the mesh.

---

## 1. Shape: throttled breadth-first search

The crawl is a **breadth-first search** over the RF-adjacency graph, seeded at
the companion node, bounded by depth, deduplicated by a visited-set, and paced by
a global rate limiter.

BFS (not depth-first) because:

- Depth is the natural stopping bound the operator sets (FR-19), and BFS reaches
  every node at hop *k* before any node at hop *k+1* — so "depth 5" means exactly
  "everything within 5 hops," and if the run is cut short by time/budget you've
  covered the *nearest* nodes, which are the most relevant.
- The nearest nodes are the ones most likely to be TwinOak's own or its essential
  hops, so covering them first is the right failure mode.

## 2. State

```
graph        : MeshGraph          // persisted; keyed by nodeId (public key)
visited      : Set<NodeId>        // queried this run (never re-query)
enqueued     : Set<NodeId>        // already in the frontier (never double-enqueue)
frontier     : Queue<(NodeId, int depth)>   // FIFO → breadth-first
budget       : { maxDepth, maxNodes?, deadline? }
counters     : { requestsSent, nodesQueried, newNodes, failures }
```

`visited` and `enqueued` are distinct: a node is `enqueued` the moment it's
discovered (so it's never queued twice), and moves to `visited` when its query
plan has run. A node can appear as a neighbour of many others; it is enqueued at
most once, at the *shallowest* depth it's first seen.

**Node identity.** Nodes are keyed on their **stable public key / node ID**, not
their name (names aren't unique and can change). Until a node's key is known it
is held under a provisional key derived from the advert/contact that referenced
it, and merged once the real key is read. See
[DATA-MODEL.md §identity](DATA-MODEL.md).

## 3. Main loop (pseudocode)

```csharp
async Task CrawlAsync(CrawlOptions opt, CancellationToken ct)
{
    var self = await client.GetSelfAsync();                 // local, no airtime
    graph.UpsertSelf(self);

    // Seed: companion's own neighbours (hop 1) + known contacts (FR-5, FR-7)
    foreach (var n in await client.GetSelfNeighboursAsync())   // may cost airtime
        Enqueue(n.NodeId, depth: 1);
    foreach (var c in await client.GetContactsAsync())         // local, no airtime
        Enqueue(c.NodeId, depth: 1, seedOnly: true);

    while (frontier.TryDequeue(out var item) && !BudgetExhausted(opt))
    {
        ct.ThrowIfCancellationRequested();
        var (nodeId, depth) = item;
        if (visited.Contains(nodeId)) continue;               // belt & braces

        var node = graph.GetOrCreate(nodeId);
        node.LastCrawlAttempt = now;

        var result = await QueryNodeAsync(node, opt, ct);      // the per-node plan
        visited.Add(nodeId);
        counters.nodesQueried++;

        // Enqueue neighbours we haven't seen — but only if we may go deeper
        if (depth < opt.MaxDepth)
            foreach (var nb in result.Neighbours)
            {
                graph.RecordEdge(nodeId, nb);                  // record the edge either way
                Enqueue(nb.NodeId, depth + 1);
            }
        else
            foreach (var nb in result.Neighbours)
                graph.RecordEdge(nodeId, nb);                  // record, don't descend

        await store.SaveAsync(graph);                          // incremental persist (FR-29)
    }

    graph.CloseRun(counters, reason: BudgetExhausted(opt) ? ... : "frontier-empty");
    await store.SaveAsync(graph);
}

void Enqueue(NodeId id, int depth, bool seedOnly = false)
{
    if (visited.Contains(id) || enqueued.Contains(id)) return;
    if (depth > opt.MaxDepth && !seedOnly) { graph.NoteBeyondDepth(id); return; }
    enqueued.Add(id);
    frontier.Enqueue((id, depth));
}
```

Key invariants:

- **No node is ever queried twice** — guarded by `visited` and `enqueued`
  (FR-20, NFR-4).
- **Termination is guaranteed** — the frontier only ever contains not-yet-visited
  nodes, each node enters it at most once, and the node set is finite. Even a
  fully-connected mesh of cycles terminates (NFR-4).
- **Edges are recorded even past the depth bound** — so the graph knows node X at
  depth 5 has a neighbour Y even though Y is never itself queried (FR-19). Y shows
  up in the data as "referenced, not crawled."

## 4. Per-node query plan

`QueryNodeAsync` runs an ordered sequence of asks. Every step that reaches over
the air passes through the rate limiter; local reads don't. Order is chosen so
the **cheapest, most identifying** data comes first — if the run is cut off or
the node drops after step 2, we still have identity and version.

For a **reachable repeater/room server**:

1. **Login** (if a password is configured for it or a default exists) →
   `PUSH_CODE_LOGIN_SUCCESS/FAIL`. On fail: record `auth-failed`, continue with
   whatever anonymous data exists, skip admin-gated steps. *(1 OTA request)*
2. **`ver` + `board` + `get role`** — firmware, hardware, role. *(batched where
   the protocol allows; otherwise counted individually)*
3. **`get public.key` + `get name`** — stabilise identity (FR-12).
4. **`get owner.info`** — owner (FR-8).
5. **`neighbors`** — the neighbour table (FR-6, FR-11). **The one step that must
   succeed for the crawl to progress from this node.**
6. **`region list` / `region default`** — configured scopes (FR-10).
7. *(optional, off by default)* **`stats-radio`**, **`clock`** — link health,
   clock drift (FR-13).
8. *(optional, off by default)* **telemetry** (FR-14), **position** (FR-15),
   **path/trace** enrichment.

Each result is written onto the node record as it arrives, with a per-field
"as-of" timestamp and a `source` (which command produced it). A step that fails
is recorded as a field-level error, not a node-level abort.

**Request-count awareness.** The plan is written so the operator can see, and
cap, how many OTA requests each node costs. At the default 1/min, a full
8-request plan is 8 minutes per node — so the plan is **tiered**: a *core* tier
that the crawl always runs, and an *extended* tier (stats, telemetry, trace) that
is opt-in via config. The core tier bills **~5 over-the-air requests per node** —
login, `ver` (with `board`/`role` batched into the same exchange where the
firmware allows), identity (`public.key`/`name`, likewise batched or taken from
the advert), `owner.info`, `neighbors`, and region config — so plan on roughly
5 requests/node for budgeting. This keeps the per-night node count meaningful.
See §7.

For a **node that is only a contact/advert** (heard but not reachable as a
loginable repeater), the "query" degrades to recording the advert-derived fields
(name, key, position, last-heard) with no OTA cost, and marking it
`referenced` / `not-directly-queried`.

## 5. The rate limiter

A **token-bucket** limiter parameterised in **messages per minute**:

- Capacity and refill are derived from the configured rate. At the default
  1 msg/min, the bucket holds 1 token and refills 1 token/60 s — i.e. strictly
  one request per minute, no bursting.
- A small burst allowance may be configured (e.g. "1/min but allow a burst of 3")
  for operators who know the channel is quiet; **off by default**.
- **Every** `IMeshClient` method that emits an over-the-air request calls
  `await limiter.AcquireAsync(ct)` immediately before sending. Local-only calls
  (self-info, contacts already cached on the companion) do not.
- While the crawler waits for a token it logs "throttled: next request in Ns" so
  an operator watching the console understands the pauses (FR-33).

Because the limiter sits *inside* the client and *every* OTA method awaits it,
there is no code path that reaches the air without paying the toll
([FR-24](REQUIREMENTS.md#16-throttling-first-class-not-an-afterthought)).

Optional adaptive back-off (FR-27): if the companion reports send failures or
channel-busy, the limiter's effective rate is temporarily halved and recovers
slowly — so a struggling network makes the crawler back off, never lean in.

## 6. Depth, budgets and termination

The crawl stops when **any** bound is hit:

- **Frontier empty** — the whole reachable mesh within `maxDepth` is covered.
  The natural, clean finish.
- **`maxDepth`** — nodes deeper than the bound are recorded as referenced but not
  queried (FR-19).
- **`maxNodes`** (optional, FR-21) — a hard cap on nodes queried this run.
- **`deadline`** (optional, FR-22) — wall-clock stop, so an overnight run is
  guaranteed to finish by, say, 06:00. On deadline, the current node is
  finished/abandoned, state is flushed, and the run closes as `incomplete` with
  the frontier persisted so the *next* night can optionally continue where this
  one left off.

## 7. Budgeting a night — worked example

The operator needs to be able to reason about coverage before pointing this at
the mesh. With the core query tier at ~5 OTA requests per node:

| Rate | Requests/night (10 h) | Nodes/night (core tier, ~5 req each) |
|---:|---:|---:|
| 1 msg/min | 600 | ~120 |
| 2 msg/min | 1 200 | ~240 |
| 6 msg/min | 3 600 | ~720 |

These are ceilings — real nodes cost retries, some login attempts fail, and
depth may be reached before the budget is. But it shows the PoC's central
question is quantitatively answerable: at the safe 1/min default, an overnight
run can realistically characterise on the order of ~120 nodes to the core tier
(fewer once retries and failed logins are counted), which for the TwinOak mesh is
plenty of headroom. If the mesh is smaller than the
budget, the crawl simply finishes early and idles until next night.

`--dry-run` (FR-35) prints exactly this estimate for the configured seed set,
depth and rate before any packet is sent.

## 8. Multi-night behaviour

Because the graph, first/last-seen timestamps and (optionally) the unfinished
frontier are persisted, successive nights compose:

- Nodes seen before are refreshed (their `lastCrawled` advances, changed fields
  are updated, and — crucially for the fleet manager — **drift is detectable**:
  a firmware version that changed, a region config that changed, a neighbour that
  disappeared).
- A `--continue` mode can resume an `incomplete` run's frontier instead of
  reseeding, to finish covering a large mesh across two nights.
- A `--refresh-older-than <duration>` mode re-queries only nodes whose data is
  stale, spending the night's budget on what's actually gone out of date rather
  than re-walking everything.

This is where NightCrawler stops being a one-shot toy and becomes the **inventory
and drift feed** the [fleet manager](../MeshCore.FleetManager/) consumes.
