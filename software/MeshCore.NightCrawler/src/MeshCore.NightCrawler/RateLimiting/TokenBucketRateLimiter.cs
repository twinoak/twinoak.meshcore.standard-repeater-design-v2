namespace MeshCore.NightCrawler.RateLimiting;

/// <summary>
/// A token-bucket limiter parameterised in messages per minute. At the default
/// 1 msg/min the bucket holds 1 token and refills 1 token / 60 s — strictly one
/// request a minute, no bursting.
///
/// The clock and the delay are injectable so the crawl's pacing can be unit-tested
/// deterministically without real wall-clock waits.
/// </summary>
public sealed class TokenBucketRateLimiter : IRateLimiter
{
    private readonly double _refillPerSec;
    private readonly double _capacity;
    private readonly Func<DateTimeOffset> _now;
    private readonly Func<TimeSpan, CancellationToken, Task> _delay;
    private readonly SemaphoreSlim _gate = new(1, 1);

    private double _tokens;
    private DateTimeOffset _last;

    public double RatePerMinute { get; }
    public long Acquired { get; private set; }
    public TimeSpan TotalWait { get; private set; }
    public event Action<TimeSpan>? Throttled;

    public TokenBucketRateLimiter(
        double ratePerMinute,
        double burst = 1,
        Func<DateTimeOffset>? now = null,
        Func<TimeSpan, CancellationToken, Task>? delay = null)
    {
        if (ratePerMinute <= 0) throw new ArgumentOutOfRangeException(nameof(ratePerMinute));
        RatePerMinute = ratePerMinute;
        _refillPerSec = ratePerMinute / 60.0;
        _capacity = Math.Max(1, burst);
        _now = now ?? (() => DateTimeOffset.UtcNow);
        _delay = delay ?? ((d, ct) => Task.Delay(d, ct));
        _tokens = _capacity;           // start full: the first request goes immediately
        _last = _now();
    }

    public async Task AcquireAsync(CancellationToken ct)
    {
        await _gate.WaitAsync(ct);
        try
        {
            while (true)
            {
                Refill();
                if (_tokens >= 1.0)
                {
                    _tokens -= 1.0;
                    Acquired++;
                    return;
                }

                double deficit = 1.0 - _tokens;
                var wait = TimeSpan.FromSeconds(deficit / _refillPerSec);
                TotalWait += wait;
                Throttled?.Invoke(wait);
                await _delay(wait, ct);
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    private void Refill()
    {
        var now = _now();
        double elapsed = (now - _last).TotalSeconds;
        if (elapsed <= 0) return;
        _tokens = Math.Min(_capacity, _tokens + elapsed * _refillPerSec);
        _last = now;
    }
}
