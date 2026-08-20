using MeshCore.NightCrawler.Mesh;

namespace MeshCore.NightCrawler.Tests;

/// <summary>
/// A scripted mesh with no radio in the loop. Nodes are keyed by full 32-byte
/// hex keys; neighbours are returned as 6-byte prefixes exactly like the firmware.
/// A key present as a seed but absent from the mesh models an unreachable node.
/// </summary>
public sealed class FakeMeshClient : IMeshClient
{
    public sealed record FakeNode(
        string Key,
        string Name,
        string[] NeighbourKeys,
        string[] FloodAllowed,
        bool FloodsUnscoped,
        bool LoginOk);

    private readonly Dictionary<string, FakeNode> _mesh;
    private readonly List<string> _seeds;

    public Dictionary<string, int> LoginCallsPerKey { get; } = new();
    public int ScopeCalls { get; private set; }

    public SelfInfo? Self { get; } =
        new SelfInfo(new string('0', 64), "companion", OpCodes.AdvTypeChat, 869.618, 62.5, 8, 8);

    public event Action<ContactInfo>? NewContactHeard;

    public FakeMeshClient(IEnumerable<FakeNode> mesh, IEnumerable<string> seeds)
    {
        _mesh = mesh.ToDictionary(n => n.Key);
        _seeds = seeds.ToList();
    }

    /// <summary>A distinct 32-byte (64 hex char) key derived from a single byte, e.g. 0xAB → "abab…ab".</summary>
    public static string Key(byte b) => string.Concat(Enumerable.Repeat(b.ToString("x2"), 32));

    public Task ConnectAndHandshakeAsync(CancellationToken ct) => Task.CompletedTask;

    public Task<IReadOnlyList<ContactInfo>> GetContactsAsync(CancellationToken ct)
    {
        IReadOnlyList<ContactInfo> list = _seeds
            .Select(k => new ContactInfo(k, OpCodes.AdvTypeRepeater, -1, "", -1,
                _mesh.TryGetValue(k, out var n) ? n.Name : "unknown", 0, 0, 0))
            .ToList();
        return Task.FromResult(list);
    }

    public Task<ContactInfo?> RefreshContactAsync(string pubKeyHex, CancellationToken ct)
    {
        // After login a direct (zero-hop) path is known.
        ContactInfo? c = _mesh.ContainsKey(pubKeyHex)
            ? new ContactInfo(pubKeyHex, OpCodes.AdvTypeRepeater, 0, "", 0, _mesh[pubKeyHex].Name, 0, 0, 0)
            : null;
        return Task.FromResult(c);
    }

    public Task<GuestLoginResult> GuestLoginAsync(ContactInfo node, IReadOnlyList<string> candidatePasswords, CancellationToken ct)
    {
        LoginCallsPerKey[node.PublicKey] = LoginCallsPerKey.GetValueOrDefault(node.PublicKey) + 1;
        if (_mesh.TryGetValue(node.PublicKey, out var n) && n.LoginOk)
            return Task.FromResult(new GuestLoginResult(true, OpCodes.PermGuest, 2, 0));
        return Task.FromResult(GuestLoginResult.Failed);
    }

    public Task<IReadOnlyList<NeighbourEntry>?> GetNeighboursAsync(ContactInfo node, CancellationToken ct)
    {
        if (!_mesh.TryGetValue(node.PublicKey, out var n) || !n.LoginOk)
            return Task.FromResult<IReadOnlyList<NeighbourEntry>?>(null);
        IReadOnlyList<NeighbourEntry> list = n.NeighbourKeys
            .Select(k => new NeighbourEntry(k, 7.5, 30))   // full keys (client requests 32-byte)
            .ToList();
        return Task.FromResult<IReadOnlyList<NeighbourEntry>?>(list);
    }

    public Task EnsureContactAsync(string pubKeyHex, string name, byte type, CancellationToken ct)
        => Task.CompletedTask;   // the fake mesh is queryable regardless of contact state

    public Task<OwnerInfo?> GetOwnerInfoAsync(ContactInfo node, CancellationToken ct)
    {
        if (!_mesh.TryGetValue(node.PublicKey, out var n) || !n.LoginOk)
            return Task.FromResult<OwnerInfo?>(null);
        return Task.FromResult<OwnerInfo?>(new OwnerInfo(n.Name, "owner@twinoak", "v1.17.1"));
    }

    public Task<OwnerInfo?> GetOwnerAnonAsync(ContactInfo node, CancellationToken ct)
    {
        if (!_mesh.TryGetValue(node.PublicKey, out var n))
            return Task.FromResult<OwnerInfo?>(null);
        return Task.FromResult<OwnerInfo?>(new OwnerInfo(n.Name, "owner@twinoak", null));
    }

    public Task<ScopeInfo?> GetScopesAsync(ContactInfo node, CancellationToken ct)
    {
        ScopeCalls++;
        if (!_mesh.TryGetValue(node.PublicKey, out var n))
            return Task.FromResult<ScopeInfo?>(null);
        string raw = string.Join(",", (n.FloodsUnscoped ? new[] { "*" } : Array.Empty<string>()).Concat(n.FloodAllowed));
        return Task.FromResult<ScopeInfo?>(new ScopeInfo(n.FloodAllowed, n.FloodsUnscoped, raw));
    }

    public ValueTask DisposeAsync() => ValueTask.CompletedTask;

    // exposed so a test could inject an advert if it wanted
    public void RaiseAdvert(ContactInfo c) => NewContactHeard?.Invoke(c);
}
