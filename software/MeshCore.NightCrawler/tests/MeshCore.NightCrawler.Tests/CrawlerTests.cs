using MeshCore.NightCrawler;
using MeshCore.NightCrawler.Crawl;
using MeshCore.NightCrawler.Model;
using MeshCore.NightCrawler.RateLimiting;
using MeshCore.NightCrawler.Storage;
using Xunit;

namespace MeshCore.NightCrawler.Tests;

public class CrawlerTests
{
    private sealed class InMemoryStore : IGraphStore
    {
        public MeshGraph Graph = new();
        public int Saves;
        public Task<MeshGraph> LoadAsync(CancellationToken ct) => Task.FromResult(Graph);
        public Task SaveAsync(MeshGraph graph, CancellationToken ct) { Graph = graph; Saves++; return Task.CompletedTask; }
    }

    // A limiter that never actually waits, so the traversal runs instantly but the
    // request count is still tracked truthfully.
    private static TokenBucketRateLimiter FastLimiter() =>
        new(ratePerMinute: 1_000_000, burst: 1_000_000, delay: (_, _) => Task.CompletedTask);

    private static (Crawler, InMemoryStore, MeshGraph) Build(FakeMeshClient client, CrawlOptions opt)
    {
        var store = new InMemoryStore();
        var graph = new MeshGraph();
        store.Graph = graph;
        var crawler = new Crawler(client, store, FastLimiter(), graph, opt, _ => { });
        return (crawler, store, graph);
    }

    private static FakeMeshClient.FakeNode Node(byte id, byte[] neighbours, string[] scopes, bool unscoped = false, bool loginOk = true)
        => new(FakeMeshClient.Key(id), $"node-{id:x2}",
               neighbours.Select(FakeMeshClient.Key).ToArray(), scopes, unscoped, loginOk);

    [Fact]
    public async Task Cyclic_mesh_terminates_and_never_double_queries()
    {
        // A ↔ B ↔ C ↔ A — a fully cyclic mesh.
        var a = Node(0xA0, new byte[] { 0xB0, 0xC0 }, new[] { "DK" });
        var b = Node(0xB0, new byte[] { 0xA0, 0xC0 }, new[] { "DK" });
        var c = Node(0xC0, new byte[] { 0xA0, 0xB0 }, new[] { "DK" });
        var client = new FakeMeshClient(new[] { a, b, c }, new[] { a.Key, b.Key, c.Key });
        var (crawler, _, graph) = Build(client, new CrawlOptions { MaxDepth = 5 });

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal("frontier-empty", summary.Reason);
        Assert.Equal(3, summary.NodesQueried);
        // Each node logged in exactly once despite the cycles.
        Assert.All(new[] { a.Key, b.Key, c.Key }, k => Assert.Equal(1, client.LoginCallsPerKey[k]));
        // Edges recorded for the adjacency.
        Assert.Contains(graph.Edges, e => e.From == a.Key && e.To == b.Key);
    }

    [Fact]
    public async Task Scopes_are_mapped_and_mismatch_is_detected()
    {
        // A floods un-scoped; B is locked to DK. They are neighbours → a mismatch.
        var a = Node(0xA0, new byte[] { 0xB0 }, Array.Empty<string>(), unscoped: true);
        var b = Node(0xB0, new byte[] { 0xA0 }, new[] { "DK" }, unscoped: false);
        var client = new FakeMeshClient(new[] { a, b }, new[] { a.Key, b.Key });
        var (crawler, _, graph) = Build(client, new CrawlOptions { MaxDepth = 5 });

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal(2, summary.ScopesMapped);
        Assert.Equal(1, summary.UnscopedNodes);
        Assert.Equal(1, summary.ScopedNodes);
        Assert.True(summary.ScopeMismatchEdges >= 1);
        Assert.Contains(graph.Edges, e => e.ScopeMatch == "differ");
        Assert.True(graph.Nodes[a.Key].Scopes!.FloodsUnscoped);
        Assert.False(graph.Nodes[b.Key].Scopes!.FloodsUnscoped);
    }

    [Fact]
    public async Task Login_failure_still_records_scopes_as_guest_auth_failed()
    {
        var a = Node(0xA0, Array.Empty<byte>(), new[] { "DK" }, loginOk: false);
        var client = new FakeMeshClient(new[] { a }, new[] { a.Key });
        var (crawler, _, graph) = Build(client, new CrawlOptions());

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal(1, summary.ScopesMapped);           // scopes survive login failure
        Assert.Equal(1, summary.GuestAuthFailures);
        Assert.Equal(NodeStatus.GuestAuthFailed, graph.Nodes[a.Key].Status);
        Assert.False(graph.Nodes[a.Key].Access.GuestLoginSucceeded);
        Assert.NotNull(graph.Nodes[a.Key].Scopes);
    }

    [Fact]
    public async Task Seed_not_in_mesh_is_marked_unreachable()
    {
        string ghost = FakeMeshClient.Key(0xEE);
        var client = new FakeMeshClient(Array.Empty<FakeMeshClient.FakeNode>(), new[] { ghost });
        var (crawler, _, graph) = Build(client, new CrawlOptions());

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal(1, summary.Unreachable);
        Assert.Equal(NodeStatus.Unreachable, graph.Nodes[ghost].Status);
    }

    [Fact]
    public async Task Depth_zero_queries_nothing()
    {
        var a = Node(0xA0, new byte[] { 0xB0 }, new[] { "DK" });
        var client = new FakeMeshClient(new[] { a }, new[] { a.Key });
        var (crawler, _, _) = Build(client, new CrawlOptions { MaxDepth = 0 });

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal(0, summary.NodesQueried);  // seeds enter at depth 1 > maxDepth 0
    }

    [Fact]
    public async Task Max_nodes_budget_stops_early()
    {
        var nodes = Enumerable.Range(1, 5)
            .Select(i => Node((byte)i, Array.Empty<byte>(), new[] { "DK" })).ToArray();
        var client = new FakeMeshClient(nodes, nodes.Select(n => n.Key));
        var (crawler, _, _) = Build(client, new CrawlOptions { MaxNodes = 2 });

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal("max-nodes", summary.Reason);
        Assert.Equal(2, summary.NodesQueried);
    }

    [Fact]
    public async Task Configured_seed_starts_there_and_only_walks_its_neighbours()
    {
        // a→b are neighbours; c is a contact but disconnected from a.
        var a = Node(0xA0, new byte[] { 0xB0 }, new[] { "DK" });
        var b = Node(0xB0, new byte[] { 0xA0 }, new[] { "DK" });
        var c = Node(0xC0, Array.Empty<byte>(), new[] { "DK" });
        // All three are contacts, but we seed only 'a'.
        var client = new FakeMeshClient(new[] { a, b, c }, new[] { a.Key, b.Key, c.Key });
        var (crawler, _, graph) = Build(client, new CrawlOptions { Seeds = { a.Key } });

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal(2, summary.NodesQueried);                 // a and its neighbour b
        Assert.True(client.LoginCallsPerKey.ContainsKey(a.Key));
        Assert.True(client.LoginCallsPerKey.ContainsKey(b.Key));
        Assert.False(client.LoginCallsPerKey.ContainsKey(c.Key)); // c never seeded, never reached
    }

    [Fact]
    public async Task Full_key_seed_not_in_contacts_is_still_crawled()
    {
        // The seed exists in the mesh but is NOT in the companion's contact list.
        var x = Node(0xAB, Array.Empty<byte>(), new[] { "DK" });
        var client = new FakeMeshClient(new[] { x }, Array.Empty<string>()); // no contacts
        var (crawler, _, graph) = Build(client, new CrawlOptions { Seeds = { x.Key } });

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal(1, summary.NodesQueried);
        Assert.Equal(1, summary.ScopesMapped);
        Assert.Equal(NodeStatus.Crawled, graph.Nodes[x.Key].Status);
    }

    [Fact]
    public async Task Neighbours_that_are_not_contacts_are_still_followed()
    {
        // The regression from the field: a's neighbours b and d are NOT companion
        // contacts, yet the crawl must register + follow them, not drop them.
        var a = Node(0xA0, new byte[] { 0xB0, 0xD0 }, new[] { "DK" });
        var b = Node(0xB0, Array.Empty<byte>(), new[] { "DK" });
        var d = Node(0xD0, Array.Empty<byte>(), new[] { "DK" });
        var client = new FakeMeshClient(new[] { a, b, d }, new[] { a.Key }); // only 'a' is a contact
        var (crawler, _, graph) = Build(client, new CrawlOptions { Seeds = { a.Key } });

        var summary = await crawler.RunAsync(CancellationToken.None);

        Assert.Equal(3, summary.NodesQueried);   // a AND both non-contact neighbours
        Assert.Equal(NodeStatus.Crawled, graph.Nodes[b.Key].Status);
        Assert.Equal(NodeStatus.Crawled, graph.Nodes[d.Key].Status);
    }
}
