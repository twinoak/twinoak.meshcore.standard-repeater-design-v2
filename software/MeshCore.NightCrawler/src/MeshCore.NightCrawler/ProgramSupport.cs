using System.Globalization;
using System.Text.Json;

namespace MeshCore.NightCrawler;

/// <summary>Minimal command-line parser: --flag, --flag value, --flag=value.</summary>
public static class ArgParser
{
    public static Dictionary<string, string> Parse(string[] args)
    {
        var d = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < args.Length; i++)
        {
            var tok = args[i];
            if (!tok.StartsWith("--")) continue;
            var key = tok[2..];
            if (key.Contains('='))
            {
                var eq = key.IndexOf('=');
                d[key[..eq]] = key[(eq + 1)..];
            }
            else if (i + 1 < args.Length && !args[i + 1].StartsWith("--"))
            {
                d[key] = args[++i];
            }
            else
            {
                d[key] = ""; // boolean flag
            }
        }
        return d;
    }
}

public static class TimeParsing
{
    public static DateTimeOffset ParseDeadline(string s)
    {
        if (s.Contains('T') || s.Contains('-'))
            return DateTimeOffset.Parse(s, CultureInfo.InvariantCulture);
        var t = TimeOnly.Parse(s, CultureInfo.InvariantCulture);
        var now = DateTimeOffset.Now;
        var today = new DateTimeOffset(now.Year, now.Month, now.Day, t.Hour, t.Minute, 0, now.Offset);
        return today <= now ? today.AddDays(1) : today;
    }
}

/// <summary>Loads the subset of appsettings.json NightCrawler honours (tolerant of missing keys).</summary>
public static class AppSettings
{
    public static void ApplyInto(CrawlOptions o, string path)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        var root = doc.RootElement;

        if (root.TryGetProperty("companion", out var comp))
        {
            if (comp.TryGetProperty("host", out var h) && h.ValueKind == JsonValueKind.String) o.Host = h.GetString()!;
            if (comp.TryGetProperty("port", out var p) && p.TryGetInt32(out var pi)) o.Port = pi;
        }
        if (root.TryGetProperty("crawl", out var crawl))
        {
            if (crawl.TryGetProperty("maxDepth", out var v) && v.TryGetInt32(out var md)) o.MaxDepth = md;
            if (crawl.TryGetProperty("ratePerMinute", out var r) && r.TryGetDouble(out var rd)) o.RatePerMinute = rd;
            if (crawl.TryGetProperty("burst", out var b) && b.TryGetDouble(out var bd)) o.Burst = bd;
            if (crawl.TryGetProperty("maxNodes", out var mn) && mn.ValueKind == JsonValueKind.Number && mn.TryGetInt32(out var mni)) o.MaxNodes = mni;
            if (crawl.TryGetProperty("scopesOnly", out var so) && (so.ValueKind == JsonValueKind.True || so.ValueKind == JsonValueKind.False)) o.ScopesOnly = so.GetBoolean();
            if (crawl.TryGetProperty("includeContacts", out var ic) && (ic.ValueKind == JsonValueKind.True || ic.ValueKind == JsonValueKind.False)) o.IncludeContacts = ic.GetBoolean();
            if (crawl.TryGetProperty("replyTimeoutSeconds", out var rts) && rts.TryGetDouble(out var rtsd)) o.ReplyTimeout = TimeSpan.FromSeconds(rtsd);
            if (crawl.TryGetProperty("pathHashSizeBytes", out var phs) && phs.TryGetInt32(out var phsi)) o.PathHashSizeBytes = phsi;
            if (crawl.TryGetProperty("setPathHashMode", out var sph) && (sph.ValueKind == JsonValueKind.True || sph.ValueKind == JsonValueKind.False)) o.SetPathHashMode = sph.GetBoolean();
            if (crawl.TryGetProperty("deadline", out var dl) && dl.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(dl.GetString()))
                o.Deadline = TimeParsing.ParseDeadline(dl.GetString()!);
        }
        if (root.TryGetProperty("seeds", out var seeds)
            && seeds.TryGetProperty("keys", out var keys) && keys.ValueKind == JsonValueKind.Array)
        {
            var list = new List<string>();
            foreach (var item in keys.EnumerateArray())
                if (item.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(item.GetString())) list.Add(item.GetString()!.Trim());
            if (list.Count > 0) o.Seeds = list;
        }
        if (root.TryGetProperty("output", out var outp))
        {
            if (outp.TryGetProperty("path", out var op) && op.ValueKind == JsonValueKind.String) o.OutputPath = op.GetString()!;
        }
        if (root.TryGetProperty("guestAuth", out var ga)
            && ga.TryGetProperty("candidatePasswords", out var cp) && cp.ValueKind == JsonValueKind.Array)
        {
            var list = new List<string>();
            foreach (var item in cp.EnumerateArray())
                if (item.ValueKind == JsonValueKind.String) list.Add(item.GetString() ?? "");
            if (list.Count > 0) o.GuestPasswords = list;
        }
    }
}

public static class Usage
{
    public const string Text = """
        NightCrawler v0.1 — a nightly MeshCore scope & neighbour crawler.

        Usage:
          nightcrawler [--host <ip>] [options]

        Connection:
          --host <ip>            Companion host (default 192.168.0.186).
          --port <n>             Companion TCP port (default 5000).
          --reply-timeout <s>    Seconds to wait for an over-the-air reply (default 30).

        Crawl:
          --seeds a,b,c          Crawl seeds: public keys (64 hex), key prefixes, or names.
                                 If set, the crawl starts here instead of every contact.
          --depth <n>            Max hops from the seed (default 5).
          --rate <n>             Max over-the-air messages/minute (default 1). Warns above 6.
          --burst <n>            Token-bucket burst (default 1 = no bursting).
          --max-nodes <n>        Stop after querying N nodes.
          --deadline <HH:mm|iso> Wall-clock stop (e.g. 06:00).
          --path-hash-size <n>   Path hop-hash size in bytes: 1, 2 or 3 (default 2, the DK mesh default).
          --set-path-hash-mode   Push that size to the companion (device change; off by default = warn only).
          --scopes-only          Census scopes+owner only; skip neighbours/version descent.
          --no-contacts          Do not seed from the companion's contacts.
          --guest-passwords a,b  Candidate guest passwords, comma-separated (empty item = blank).
                                 Default: ",hello" (blank and 'hello'). Admin is never attempted.

        Output:
          --output <path>        Graph JSON file (default mesh-graph.json).
          --dry-run              Plan only: print seeds + budget, send nothing over the air.
          --verbose              Frame-level logging.
          --config <path>        Config file (default appsettings.json).
          --help                 This help.
        """;
}
