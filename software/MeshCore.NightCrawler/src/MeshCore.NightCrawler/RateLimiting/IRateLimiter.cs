namespace MeshCore.NightCrawler.RateLimiting;

/// <summary>
/// The single choke point every over-the-air request passes through. The client
/// awaits a token immediately before each mesh-bound send, so it is structurally
/// impossible for the crawl to reach the air without paying the toll.
/// </summary>
public interface IRateLimiter
{
    /// <summary>Blocks until a token is available. Local (no-airtime) work never calls this.</summary>
    Task AcquireAsync(CancellationToken ct);

    double RatePerMinute { get; }

    /// <summary>Total tokens acquired so far — i.e. OTA requests injected into the mesh.</summary>
    long Acquired { get; }

    /// <summary>Cumulative time spent blocked on the throttle.</summary>
    TimeSpan TotalWait { get; }

    /// <summary>Raised when a caller has to wait, with the wait duration (for console feedback).</summary>
    event Action<TimeSpan>? Throttled;
}
