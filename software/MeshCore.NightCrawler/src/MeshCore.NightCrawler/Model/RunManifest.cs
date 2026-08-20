namespace MeshCore.NightCrawler.Model;

public sealed class RunCounters
{
    public int RequestsSent { get; set; }
    public int NodesQueried { get; set; }
    public int ScopesMapped { get; set; }     // primary KPI: nodes whose scope set was read
    public int FullyCrawled { get; set; }     // also got neighbours + version
    public int NewNodes { get; set; }
    public int Refreshed { get; set; }
    public int GuestAuthFailures { get; set; }
    public int Unreachable { get; set; }
    public double ThrottleWaitSeconds { get; set; }
}

public sealed class RunManifest
{
    public string RunId { get; set; } = "";
    public DateTimeOffset StartedAt { get; set; }
    public DateTimeOffset? EndedAt { get; set; }
    public string Reason { get; set; } = "";   // frontier-empty | max-depth | max-nodes | deadline | cancelled | transport-lost
    public Dictionary<string, object?> Config { get; set; } = new();
    public RunCounters Counters { get; set; } = new();
}
