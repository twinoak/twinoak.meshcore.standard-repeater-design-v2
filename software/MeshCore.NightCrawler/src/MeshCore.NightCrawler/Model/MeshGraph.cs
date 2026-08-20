namespace MeshCore.NightCrawler.Model;

public sealed class HomeChannel
{
    public double FreqMHz { get; set; } = 869.618;
    public double BwKHz { get; set; } = 62.5;
    public int Sf { get; set; } = 8;
    public int Cr { get; set; } = 8;
}

public sealed class NetworkInfo
{
    public string Name { get; set; } = "TwinOak / MeshCore Denmark";
    public HomeChannel HomeChannel { get; set; } = new();
}

public sealed class CompanionInfo
{
    public string PublicKey { get; set; } = "";
    public string Name { get; set; } = "";
    public string Host { get; set; } = "";
    public int Port { get; set; }
}

public sealed class Edge
{
    public string From { get; set; } = "";
    public string To { get; set; } = "";
    public double? SnrDb { get; set; }
    public bool Directed { get; set; } = true;
    public string ObservedVia { get; set; } = "";
    public string ScopeMatch { get; set; } = "unknown";  // same | differ | unknown
    public DateTimeOffset AsOf { get; set; }
}

/// <summary>The whole persisted graph — one file, keyed by node public key.</summary>
public sealed class MeshGraph
{
    public int SchemaVersion { get; set; } = 1;
    public NetworkInfo Network { get; set; } = new();
    public CompanionInfo? Companion { get; set; }
    public DateTimeOffset GeneratedAt { get; set; }
    public Dictionary<string, MeshNode> Nodes { get; set; } = new();
    public List<Edge> Edges { get; set; } = new();
    public List<RunManifest> Runs { get; set; } = new();

    public MeshNode GetOrCreate(string pubKey, DateTimeOffset now)
    {
        if (!Nodes.TryGetValue(pubKey, out var n))
        {
            n = new MeshNode { PublicKey = pubKey, FirstSeen = now, LastSeen = now };
            Nodes[pubKey] = n;
        }
        return n;
    }

    /// <summary>Record a directed edge from→to, computing the scope-match if both scopes are known.</summary>
    public void RecordEdge(string from, string to, double? snr, DateTimeOffset now)
    {
        var e = Edges.FirstOrDefault(x => x.From == from && x.To == to);
        if (e is null)
        {
            e = new Edge { From = from, To = to, ObservedVia = $"get-neighbours@{Short(from)}" };
            Edges.Add(e);
        }
        e.SnrDb = snr;
        e.AsOf = now;
        e.ScopeMatch = ScopeMatch(from, to);
    }

    /// <summary>
    /// Recompute every edge's scope-match. Edges are first recorded while the far node's
    /// scopes are still unknown, so this final pass fills them in once the whole graph is read.
    /// </summary>
    public void RefreshEdgeScopeMatches()
    {
        foreach (var e in Edges) e.ScopeMatch = ScopeMatch(e.From, e.To);
    }

    private string ScopeMatch(string a, string b)
    {
        if (!Nodes.TryGetValue(a, out var na) || !Nodes.TryGetValue(b, out var nb)) return "unknown";
        if (na.Scopes is null || nb.Scopes is null) return "unknown";
        var sa = na.Scopes; var sb = nb.Scopes;
        // Two nodes "share a scope" if both flood un-scoped, or their named
        // flood-allowed sets intersect. A pure difference (one floods *, the
        // other only DK) is the silent-hole case the crawl exists to surface.
        if (sa.FloodsUnscoped && sb.FloodsUnscoped) return "same";
        bool overlap = sa.FloodAllowedRegions.Intersect(sb.FloodAllowedRegions).Any();
        if (overlap) return "same";
        return "differ";
    }

    private static string Short(string key) => key.Length >= 12 ? key[..12] : key;
}
