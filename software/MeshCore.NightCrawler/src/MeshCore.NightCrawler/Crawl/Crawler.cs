using MeshCore.NightCrawler.Mesh;
using MeshCore.NightCrawler.Model;
using MeshCore.NightCrawler.RateLimiting;
using MeshCore.NightCrawler.Storage;

namespace MeshCore.NightCrawler.Crawl;

/// <summary>
/// A throttled breadth-first walk of the mesh. Seeded from the companion's
/// contacts, bounded by depth, deduplicated by a visited-set, and paced entirely
/// by the rate limiter inside the client. Per node the plan is: guest login →
/// (neighbours + version) → refresh path → anonymous scopes. Scopes are recorded
/// even when a login fails, so the primary datum survives auth failure.
/// </summary>
public sealed class Crawler
{
    private readonly IMeshClient _client;
    private readonly IGraphStore _store;
    private readonly IRateLimiter _limiter;
    private readonly MeshGraph _graph;
    private readonly CrawlOptions _opt;
    private readonly Action<string> _log;

    private readonly Dictionary<string, ContactInfo> _contacts = new();
    private readonly Queue<(string Key, int Depth)> _frontier = new();
    private readonly HashSet<string> _visited = new();
    private readonly HashSet<string> _enqueued = new();
    private readonly RunCounters _counters = new();

    private DateTimeOffset Now => DateTimeOffset.UtcNow;

    public Crawler(IMeshClient client, IGraphStore store, IRateLimiter limiter,
                   MeshGraph graph, CrawlOptions opt, Action<string> log)
    {
        _client = client;
        _store = store;
        _limiter = limiter;
        _graph = graph;
        _opt = opt;
        _log = log;
    }

    public async Task<CrawlSummary> RunAsync(CancellationToken ct)
    {
        var startedAt = Now;
        var preexisting = new HashSet<string>(_graph.Nodes.Keys);

        // Companion (vantage point) — recorded, never itself crawled.
        if (_client.Self is { } self)
        {
            _graph.Companion = new CompanionInfo
            {
                PublicKey = self.PublicKey, Name = self.Name, Host = _opt.Host, Port = _opt.Port
            };
            var sn = _graph.GetOrCreate(self.PublicKey, Now);
            sn.Name = self.Name;
            sn.Role = OpCodes.RoleName(self.AdvType);
            sn.Depth = 0;
            sn.Status = NodeStatus.Referenced;
            sn.LastSeen = Now;
            _visited.Add(self.PublicKey);
        }

        _client.NewContactHeard += OnAdvert;
        string reason;
        try
        {
            // Always load contacts (local, no airtime): they resolve neighbour prefixes,
            // supply reply paths, and are the default seed set.
            var contacts = await _client.GetContactsAsync(ct);
            foreach (var c in contacts)
            {
                RegisterContact(c);
                UpsertFromContact(c, depth: 1);
            }

            if (_opt.Seeds.Count > 0)
            {
                int seeded = 0, missed = 0;
                foreach (var spec in _opt.Seeds)
                {
                    var key = ResolveSeed(spec);
                    if (key is null) { _log($"seed not resolved (not a contact and not a full key): {spec}"); missed++; continue; }
                    Enqueue(key, 1);
                    seeded++;
                }
                _log($"seeded {seeded} node(s) from config{(missed > 0 ? $" ({missed} unresolved)" : "")}");
            }
            else if (_opt.IncludeContacts)
            {
                foreach (var c in contacts) Enqueue(c.PublicKey, 1);
                _log($"seeded {_frontier.Count} node(s) from {contacts.Count} companion contact(s)");
            }
            else
            {
                _log("no seeds configured and contact-seeding disabled — nothing to crawl");
            }
            await _store.SaveAsync(_graph, ct);

            reason = await CrawlLoopAsync(ct);
        }
        finally
        {
            _client.NewContactHeard -= OnAdvert;
        }

        var summary = Finalise(reason, startedAt, preexisting);
        await _store.SaveAsync(_graph, ct);
        return summary;
    }

    private async Task<string> CrawlLoopAsync(CancellationToken ct)
    {
        while (_frontier.TryDequeue(out var item))
        {
            ct.ThrowIfCancellationRequested();

            if (_opt.MaxNodes is { } cap && _counters.NodesQueried >= cap) return "max-nodes";
            if (_opt.Deadline is { } dl && Now >= dl) return "deadline";

            var (key, depth) = item;
            if (_visited.Contains(key)) continue;

            if (!_contacts.TryGetValue(key, out var contact))
            {
                // Known only by prefix (a neighbour we have no full key/path for).
                var refNode = _graph.GetOrCreate(key, Now);
                if (refNode.Status == NodeStatus.Referenced) refNode.Depth = depth;
                _visited.Add(key);
                continue;
            }

            var node = _graph.GetOrCreate(key, Now);
            node.LastCrawlAttempt = Now;
            node.Depth = node.Depth == 0 ? depth : Math.Min(node.Depth, depth);

            _log($"[d{depth}] querying {node.ShortKey} '{node.Name}'");
            await QueryNodeAsync(contact, node, depth, ct);

            _visited.Add(key);
            _counters.NodesQueried++;
            await _store.SaveAsync(_graph, ct);
        }
        return "frontier-empty";
    }

    private async Task QueryNodeAsync(ContactInfo contact, MeshNode node, int depth, CancellationToken ct)
    {
        node.Access.ReachedOverAir = true;
        node.Access.GuestLoginAttempted = true;

        var login = await _client.GuestLoginAsync(contact, _opt.GuestPasswords, ct);

        IReadOnlyList<NeighbourEntry>? neighbours = null;
        OwnerInfo? owner = null;
        ScopeInfo? scopes;

        if (login.Success)
        {
            node.Access.GuestLoginSucceeded = true;
            node.Access.GuestPasswordIndex = login.MatchedPasswordIndex;
            node.Access.PermissionTier = login.Tier;

            if (!_opt.ScopesOnly)
            {
                neighbours = await _client.GetNeighboursAsync(contact, ct);
                owner = await _client.GetOwnerInfoAsync(contact, ct);
            }

            // A successful login makes the companion learn a direct path to this
            // node; re-read it so the anonymous scope request can go direct.
            var refreshed = await _client.RefreshContactAsync(contact.PublicKey, ct) ?? contact;
            if (refreshed.HasDirectPath) _contacts[contact.PublicKey] = refreshed;
            scopes = await _client.GetScopesAsync(refreshed, ct);
        }
        else
        {
            owner = await _client.GetOwnerAnonAsync(contact, ct);
            scopes = await _client.GetScopesAsync(contact, ct);
        }

        ApplyOwner(node, owner);
        if (scopes is not null)
            node.Scopes = new ScopeRecord
            {
                FloodAllowedRegions = scopes.FloodAllowedRegions.ToList(),
                FloodsUnscoped = scopes.FloodsUnscoped,
                Raw = scopes.Raw,
            };
        node.Access.AnonReadOk = scopes is not null || (!login.Success && owner is not null);

        if (neighbours is not null) await ApplyNeighboursAsync(node, neighbours, depth, ct);

        // Status + counters.
        bool anything = scopes is not null || owner is not null || neighbours is not null;
        if (!anything)
        {
            node.Status = NodeStatus.Unreachable;
            _counters.Unreachable++;
            _log($"      unreachable ({node.ShortKey})");
            return;
        }

        node.LastCrawled = Now;
        if (scopes is not null) _counters.ScopesMapped++;

        if (login.Success && !_opt.ScopesOnly && neighbours is not null && owner is not null && scopes is not null)
        {
            node.Status = NodeStatus.Crawled;
            _counters.FullyCrawled++;
        }
        else if (_opt.ScopesOnly && scopes is not null)
        {
            node.Status = NodeStatus.ScopeOnly;
        }
        else if (!login.Success)
        {
            node.Status = NodeStatus.GuestAuthFailed;
            _counters.GuestAuthFailures++;
        }
        else
        {
            node.Status = NodeStatus.Partial;
        }

        LogNodeResult(node, scopes);
    }

    private void ApplyOwner(MeshNode node, OwnerInfo? owner)
    {
        if (owner is null) return;
        if (!string.IsNullOrWhiteSpace(owner.Name)) node.Name = owner.Name;
        if (!string.IsNullOrWhiteSpace(owner.OwnerText)) node.OwnerInfo = owner.OwnerText;
        if (!string.IsNullOrWhiteSpace(owner.FirmwareVersion)) node.FirmwareVersion = owner.FirmwareVersion;
    }

    private async Task ApplyNeighboursAsync(MeshNode node, IReadOnlyList<NeighbourEntry> neighbours, int depth, CancellationToken ct)
    {
        node.Neighbours = neighbours.Select(n => new NeighbourRecord
        {
            PublicKey = n.PubKeyPrefix,
            SnrDb = n.SnrDb,
            SecsAgo = n.SecsAgo,
            LastHeard = Now.AddSeconds(-n.SecsAgo),
        }).ToList();

        foreach (var n in neighbours)
        {
            // GET_NEIGHBOURS now returns full 32-byte keys, so every neighbour is
            // directly addressable. Register any we don't already hold as a flood-path
            // contact on the companion (local, no airtime) so it becomes queryable —
            // this is what lets the crawl follow *all* branches, not just the handful
            // that were already contacts.
            string key = n.PubKeyPrefix;
            if (key.Length != 64)
            {
                var resolved = ResolveFullKey(key);
                if (resolved is null) continue;   // only a prefix and no matching contact — record nothing to descend
                key = resolved;
            }

            _graph.RecordEdge(node.PublicKey, key, n.SnrDb, Now);

            // Only register a node as a contact if we will actually descend into it
            // (i.e. it's within the depth bound). Beyond-depth neighbours are recorded
            // as edges/referenced nodes by Enqueue but not made queryable.
            if (depth + 1 <= _opt.MaxDepth && !_contacts.ContainsKey(key))
            {
                _contacts[key] = new ContactInfo(key, OpCodes.AdvTypeRepeater, -1, "", -1, "", 0, 0, 0);
                await _client.EnsureContactAsync(key, "", OpCodes.AdvTypeRepeater, ct);
                UpsertFromContact(_contacts[key], depth + 1);
            }

            Enqueue(key, depth + 1);
        }
    }

    private string? ResolveFullKey(string prefix)
    {
        if (_contacts.ContainsKey(prefix)) return prefix; // already a full key
        foreach (var k in _contacts.Keys)
            if (k.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return k;
        return null;
    }

    /// <summary>
    /// Resolve a configured seed (full 64-hex key, hex prefix, or advertised name) to a
    /// full public key. A full key that isn't a known contact is registered as a
    /// flood-path target so we can still guest-log in to it (a login floods and works).
    /// </summary>
    private string? ResolveSeed(string spec)
    {
        spec = spec.Trim();

        if (spec.Length == 64 && IsHex(spec))
        {
            var key = spec.ToLowerInvariant();
            if (!_contacts.ContainsKey(key))
            {
                var target = new ContactInfo(key, OpCodes.AdvTypeRepeater, -1, "", -1, "", 0, 0, 0);
                _contacts[key] = target;
                UpsertFromContact(target, depth: 1);
            }
            return key;
        }

        if (spec.Length >= 2 && IsHex(spec))
        {
            var pre = spec.ToLowerInvariant();
            var match = _contacts.Keys.FirstOrDefault(k => k.StartsWith(pre, StringComparison.OrdinalIgnoreCase));
            if (match is not null) return match;
        }

        // fall back to an advertised-name match among known nodes
        var byName = _graph.Nodes.Values.FirstOrDefault(n =>
            string.Equals(n.Name, spec, StringComparison.OrdinalIgnoreCase));
        if (byName is not null && _contacts.ContainsKey(byName.PublicKey)) return byName.PublicKey;

        return null;
    }

    private static bool IsHex(string s) => s.Length > 0 && s.All(Uri.IsHexDigit);

    private void OnAdvert(ContactInfo c)
    {
        RegisterContact(c);
        UpsertFromContact(c, depth: 1);
        Enqueue(c.PublicKey, 1);
    }

    private void RegisterContact(ContactInfo c)
    {
        if (_contacts.TryGetValue(c.PublicKey, out var existing) && existing.HasDirectPath && !c.HasDirectPath)
            return; // keep the better (direct) path
        _contacts[c.PublicKey] = c;
    }

    private void UpsertFromContact(ContactInfo c, int depth)
    {
        var node = _graph.GetOrCreate(c.PublicKey, Now);
        node.LastSeen = Now;
        if (string.IsNullOrEmpty(node.Name) && !string.IsNullOrEmpty(c.Name)) node.Name = c.Name;
        if (node.Role == "unknown") node.Role = c.Role;
        if (c.Lat != 0 || c.Lon != 0) { node.Lat = c.Lat; node.Lon = c.Lon; }
        node.Depth = node.Depth == 0 ? depth : Math.Min(node.Depth, depth);
    }

    private void Enqueue(string key, int depth)
    {
        if (_visited.Contains(key) || _enqueued.Contains(key)) return;
        if (depth > _opt.MaxDepth)
        {
            var n = _graph.GetOrCreate(key, Now);
            if (n.Status == NodeStatus.Referenced) n.Status = NodeStatus.BeyondDepth;
            return;
        }
        _enqueued.Add(key);
        _frontier.Enqueue((key, depth));
    }

    private CrawlSummary Finalise(string reason, DateTimeOffset startedAt, HashSet<string> preexisting)
    {
        _graph.RefreshEdgeScopeMatches();   // fill in scope-match now that all scopes are known

        _counters.RequestsSent = (int)_limiter.Acquired;
        _counters.ThrottleWaitSeconds = _limiter.TotalWait.TotalSeconds;
        _counters.NewNodes = _graph.Nodes.Keys.Count(k => !preexisting.Contains(k));
        _counters.Refreshed = _counters.NodesQueried - _counters.NewNodes;

        int unscoped = _graph.Nodes.Values.Count(n => n.Scopes is { FloodsUnscoped: true });
        int scoped = _graph.Nodes.Values.Count(n => n.Scopes is { FloodsUnscoped: false });
        int mismatch = _graph.Edges.Count(e => e.ScopeMatch == "differ");

        var manifest = new RunManifest
        {
            RunId = "run-" + startedAt.ToString("yyyyMMdd'T'HHmm"),
            StartedAt = startedAt,
            EndedAt = Now,
            Reason = reason,
            Config = new Dictionary<string, object?>
            {
                ["maxDepth"] = _opt.MaxDepth,
                ["ratePerMinute"] = _opt.RatePerMinute,
                ["maxNodes"] = _opt.MaxNodes,
                ["scopesOnly"] = _opt.ScopesOnly,
                ["deadline"] = _opt.Deadline?.ToString("o"),
            },
            Counters = _counters,
        };
        _graph.Runs.Add(manifest);

        return new CrawlSummary
        {
            Reason = reason,
            NodesKnown = _graph.Nodes.Count,
            NodesQueried = _counters.NodesQueried,
            ScopesMapped = _counters.ScopesMapped,
            FullyCrawled = _counters.FullyCrawled,
            NewNodes = _counters.NewNodes,
            Unreachable = _counters.Unreachable,
            GuestAuthFailures = _counters.GuestAuthFailures,
            RequestsSent = _counters.RequestsSent,
            Elapsed = Now - startedAt,
            ScopedNodes = scoped,
            UnscopedNodes = unscoped,
            ScopeMismatchEdges = mismatch,
        };
    }

    private void LogNodeResult(MeshNode node, ScopeInfo? scopes)
    {
        string scopeStr = scopes is null ? "scopes=?"
            : scopes.FloodsUnscoped ? $"scopes=[*{(scopes.FloodAllowedRegions.Count > 0 ? "," + string.Join(",", scopes.FloodAllowedRegions) : "")}] (un-scoped!)"
            : $"scopes=[{string.Join(",", scopes.FloodAllowedRegions)}]";
        string ver = node.FirmwareVersion is null ? "" : $" {node.FirmwareVersion}";
        _log($"      {node.Status}: {scopeStr}{ver} · {node.Neighbours.Count} neighbour(s)");
    }
}
