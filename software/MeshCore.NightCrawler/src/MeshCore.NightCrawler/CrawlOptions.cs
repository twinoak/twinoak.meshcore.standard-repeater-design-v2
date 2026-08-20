namespace MeshCore.NightCrawler;

/// <summary>Strongly-typed run configuration. Safe defaults: 1 msg/min, depth 5.</summary>
public sealed class CrawlOptions
{
    public string Host { get; set; } = "192.168.0.186";  // the TwinOak WiFi companion
    public int Port { get; set; } = 5000;

    public int MaxDepth { get; set; } = 5;
    public double RatePerMinute { get; set; } = 1;
    public double Burst { get; set; } = 1;
    public int? MaxNodes { get; set; }
    public DateTimeOffset? Deadline { get; set; }

    public bool ScopesOnly { get; set; }
    public bool IncludeContacts { get; set; } = true;

    /// <summary>
    /// Explicit crawl seeds: full public keys (64 hex), key prefixes, or advertised
    /// names. When set, the crawl starts from these instead of every contact. Empty
    /// = fall back to seeding from the companion's contacts.
    /// </summary>
    public List<string> Seeds { get; set; } = new();

    /// <summary>Path hop-hash size in bytes (1, 2 or 3). Denmark's mesh default is 2.</summary>
    public int PathHashSizeBytes { get; set; } = 2;

    /// <summary>If true, push the configured path-hash size to the companion at startup
    /// (a device-settings change). Off by default — otherwise NightCrawler only warns
    /// when the companion disagrees.</summary>
    public bool SetPathHashMode { get; set; }

    /// <summary>MeshCore path-hash *mode* (0,1,2 → 1,2,3 bytes) derived from the size.</summary>
    public int PathHashMode => Math.Clamp(PathHashSizeBytes - 1, 0, 2);

    public List<string> GuestPasswords { get; set; } = new() { "", "hello" };

    public string OutputPath { get; set; } = "mesh-graph.json";
    public TimeSpan ReplyTimeout { get; set; } = TimeSpan.FromSeconds(30);

    public bool DryRun { get; set; }
    public bool Verbose { get; set; }
}
