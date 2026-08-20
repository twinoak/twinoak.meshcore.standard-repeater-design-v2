namespace MeshCore.NightCrawler.Mesh;

/// <summary>
/// The high-level view of a MeshCore companion that the crawler depends on.
/// Everything the traversal needs is here and nothing else, so the whole crawl
/// can be unit-tested against a scripted fake with no radio in the loop.
///
/// Local ops touch only the companion (no airtime). OTA ops inject a request into
/// the mesh and are the ones the rate limiter gates; the implementation acquires
/// a rate token *inside* each OTA method, so the crawler cannot bypass the throttle.
/// Every OTA method returns null / a failed result instead of throwing on timeout,
/// so one dead node never aborts the crawl.
/// </summary>
public interface IMeshClient : IAsyncDisposable
{
    /// <summary>Our companion's identity, available after the handshake.</summary>
    SelfInfo? Self { get; }

    /// <summary>Raised when a fresh advert is heard mid-crawl, so discovery can fold it in.</summary>
    event Action<ContactInfo>? NewContactHeard;

    /// <summary>Connect and run the app-start / device-query handshake.</summary>
    Task ConnectAndHandshakeAsync(CancellationToken ct);

    /// <summary>All contacts the companion knows (local, no airtime). Crawl seeds.</summary>
    Task<IReadOnlyList<ContactInfo>> GetContactsAsync(CancellationToken ct);

    /// <summary>Re-read one contact (local) — used to pick up a path learned after login.</summary>
    Task<ContactInfo?> RefreshContactAsync(string pubKeyHex, CancellationToken ct);

    /// <summary>
    /// Ensure a node is a contact on the companion so it can be queried (local, no airtime).
    /// Discovered neighbours arrive as bare keys and must be registered before login/requests.
    /// </summary>
    Task EnsureContactAsync(string pubKeyHex, string name, byte type, CancellationToken ct);

    // ---- OTA (rate-limited) ----

    /// <summary>Try each candidate guest password in order; stop at the first success.</summary>
    Task<GuestLoginResult> GuestLoginAsync(ContactInfo node, IReadOnlyList<string> candidatePasswords, CancellationToken ct);

    /// <summary>Read the neighbour table (guest tier). Null on failure/timeout.</summary>
    Task<IReadOnlyList<NeighbourEntry>?> GetNeighboursAsync(ContactInfo node, CancellationToken ct);

    /// <summary>Read firmware version + name + owner (guest tier). Null on failure.</summary>
    Task<OwnerInfo?> GetOwnerInfoAsync(ContactInfo node, CancellationToken ct);

    /// <summary>Read name + owner anonymously (no login). Null on failure.</summary>
    Task<OwnerInfo?> GetOwnerAnonAsync(ContactInfo node, CancellationToken ct);

    /// <summary>Read the flood-allowed scope set anonymously (needs a direct path). Null on failure.</summary>
    Task<ScopeInfo?> GetScopesAsync(ContactInfo node, CancellationToken ct);
}
