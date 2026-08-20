using MeshCore.NightCrawler.Model;

namespace MeshCore.NightCrawler.Crawl;

/// <summary>End-of-run summary, also surfaced as the run manifest.</summary>
public sealed class CrawlSummary
{
    public string Reason { get; set; } = "";
    public int NodesKnown { get; set; }
    public int NodesQueried { get; set; }
    public int ScopesMapped { get; set; }
    public int FullyCrawled { get; set; }
    public int NewNodes { get; set; }
    public int Unreachable { get; set; }
    public int GuestAuthFailures { get; set; }
    public int RequestsSent { get; set; }
    public TimeSpan Elapsed { get; set; }
    public int ScopedNodes { get; set; }        // nodes NOT flooding un-scoped
    public int UnscopedNodes { get; set; }      // nodes still flooding *
    public int ScopeMismatchEdges { get; set; } // adjacencies whose ends disagree

    public string Render()
    {
        var lines = new[]
        {
            "──────────── NightCrawler run summary ────────────",
            $"  finished:          {Reason}",
            $"  wall-clock:        {Elapsed:hh\\:mm\\:ss}",
            $"  OTA requests sent: {RequestsSent}",
            "",
            $"  nodes known:       {NodesKnown}",
            $"  nodes queried:     {NodesQueried}  (new this run: {NewNodes})",
            $"  scopes mapped:     {ScopesMapped}   ← primary objective",
            $"  fully crawled:     {FullyCrawled}",
            $"  guest-auth failed: {GuestAuthFailures}",
            $"  unreachable:       {Unreachable}",
            "",
            $"  scope picture:     {ScopedNodes} scoped · {UnscopedNodes} still flooding un-scoped",
            $"  scope mismatches:  {ScopeMismatchEdges} neighbour edge(s) whose ends disagree",
            "──────────────────────────────────────────────────",
        };
        return string.Join(Environment.NewLine, lines);
    }
}
