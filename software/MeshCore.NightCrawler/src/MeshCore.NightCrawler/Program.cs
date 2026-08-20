using System.Text.Json;
using MeshCore.NightCrawler;
using MeshCore.NightCrawler.Crawl;
using MeshCore.NightCrawler.Mesh;
using MeshCore.NightCrawler.Mesh.Transports;
using MeshCore.NightCrawler.RateLimiting;
using MeshCore.NightCrawler.Storage;

// ----------------------------------------------------------------- arg parsing
var args0 = Environment.GetCommandLineArgs()[1..];
var flags = ArgParser.Parse(args0);

if (flags.ContainsKey("help") || flags.ContainsKey("h"))
{
    Console.WriteLine(Usage.Text);
    return 0;
}

var opt = new CrawlOptions();

// defaults → appsettings.json → command-line flags
string configPath = flags.GetValueOrDefault("config") ?? "appsettings.json";
if (File.Exists(configPath))
{
    try { AppSettings.ApplyInto(opt, configPath); }
    catch (Exception e) { Console.Error.WriteLine($"warning: could not read {configPath}: {e.Message}"); }
}

try
{
    ApplyFlags(opt, flags);
}
catch (Exception e)
{
    Console.Error.WriteLine($"usage error: {e.Message}");
    Console.Error.WriteLine("run with --help for options.");
    return 64;
}

if (opt.RatePerMinute > 6)
    Console.Error.WriteLine($"WARNING: --rate {opt.RatePerMinute}/min is above the ~6/min the network is observed to tolerate.");

// ----------------------------------------------------------------- wire-up
using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); Console.Error.WriteLine("\n(cancelling — will flush and exit)"); };

void Log(string msg) => Console.WriteLine($"{DateTimeOffset.Now:HH:mm:ss} {msg}");

var limiter = new TokenBucketRateLimiter(opt.RatePerMinute, opt.Burst);
limiter.Throttled += w => { if (w.TotalSeconds >= 1) Log($"  throttled: next request in {w.TotalSeconds:0}s"); };

var transport = new TcpTransport(opt.Host, opt.Port);
await using var client = new CompanionClient(transport, limiter, opt.ReplyTimeout, Log, opt.Verbose);

Log($"NightCrawler v0.1 — connecting to companion {opt.Host}:{opt.Port}");
try
{
    await client.ConnectAndHandshakeAsync(cts.Token);
}
catch (Exception e)
{
    Console.Error.WriteLine($"could not talk to the companion at {opt.Host}:{opt.Port}: {e.Message}");
    return 1;
}

// Reconcile the path-hash size with the companion.
if (client.CompanionPathHashMode is { } mode)
{
    if (mode != opt.PathHashMode)
    {
        if (opt.SetPathHashMode)
        {
            await client.SetPathHashModeAsync(opt.PathHashMode, cts.Token);
            Log($"set companion path-hash to {opt.PathHashSizeBytes}-byte (mode {opt.PathHashMode})");
        }
        else
        {
            Console.Error.WriteLine(
                $"WARNING: companion path-hash is {mode + 1}-byte (mode {mode}) but config wants {opt.PathHashSizeBytes}-byte. " +
                "Paths may be mis-read. Pass --set-path-hash-mode to align, or set the companion manually.");
        }
    }
    else Log($"path-hash size {opt.PathHashSizeBytes}-byte matches the companion.");
}
else
{
    Log($"companion did not report a path-hash mode (older firmware); assuming {opt.PathHashSizeBytes}-byte.");
}

// ----------------------------------------------------------------- dry run
if (opt.DryRun)
{
    var contacts = await client.GetContactsAsync(cts.Token);
    double perNode = opt.ScopesOnly ? 2.5 : 5.5;
    Console.WriteLine();
    Console.WriteLine($"DRY RUN — no packets will be sent over the air.");
    Console.WriteLine($"  seeds (companion contacts): {contacts.Count}");
    Console.WriteLine($"  depth: {opt.MaxDepth}   rate: {opt.RatePerMinute}/min   mode: {(opt.ScopesOnly ? "scopes-only" : "full")}");
    Console.WriteLine($"  est. per-node cost: ~{perNode:0.0} OTA requests");
    if (opt.Deadline is { } dl)
    {
        double minutes = Math.Max(0, (dl - DateTimeOffset.Now).TotalMinutes);
        Console.WriteLine($"  budget until {dl:HH:mm}: ~{minutes * opt.RatePerMinute:0} requests → ~{minutes * opt.RatePerMinute / perNode:0} nodes");
    }
    foreach (var c in contacts.Take(20))
        Console.WriteLine($"    - {c.PublicKey[..12]} '{c.Name}' ({c.Role}, {(c.HasDirectPath ? "direct" : "flood")})");
    if (contacts.Count > 20) Console.WriteLine($"    … and {contacts.Count - 20} more");
    return 0;
}

// ----------------------------------------------------------------- crawl
var store = new JsonGraphStore(opt.OutputPath);
var graph = await store.LoadAsync(cts.Token);

var crawler = new Crawler(client, store, limiter, graph, opt, Log);

try
{
    var summary = await crawler.RunAsync(cts.Token);
    Console.WriteLine();
    Console.WriteLine(summary.Render());
    Console.WriteLine($"graph written to: {Path.GetFullPath(opt.OutputPath)}");
    // exit 0 for a planned stop; the summary reason distinguishes them
    return 0;
}
catch (OperationCanceledException)
{
    await store.SaveAsync(graph, CancellationToken.None);
    Console.Error.WriteLine("cancelled; partial graph saved.");
    return 2;
}
catch (Exception e)
{
    await store.SaveAsync(graph, CancellationToken.None);
    Console.Error.WriteLine($"crawl aborted: {e.Message}");
    return 1;
}

// ----------------------------------------------------------------- flag mapping
static void ApplyFlags(CrawlOptions o, IReadOnlyDictionary<string, string> f)
{
    if (f.TryGetValue("host", out var host)) o.Host = host;
    if (f.TryGetValue("port", out var port)) o.Port = int.Parse(port);
    if (f.TryGetValue("depth", out var depth)) o.MaxDepth = int.Parse(depth);
    if (f.TryGetValue("rate", out var rate)) o.RatePerMinute = double.Parse(rate, System.Globalization.CultureInfo.InvariantCulture);
    if (f.TryGetValue("burst", out var burst)) o.Burst = double.Parse(burst, System.Globalization.CultureInfo.InvariantCulture);
    if (f.TryGetValue("max-nodes", out var mn)) o.MaxNodes = int.Parse(mn);
    if (f.TryGetValue("output", out var outp)) o.OutputPath = outp;
    if (f.TryGetValue("reply-timeout", out var rt)) o.ReplyTimeout = TimeSpan.FromSeconds(double.Parse(rt, System.Globalization.CultureInfo.InvariantCulture));
    if (f.ContainsKey("scopes-only")) o.ScopesOnly = true;
    if (f.ContainsKey("no-contacts")) o.IncludeContacts = false;
    if (f.ContainsKey("dry-run")) o.DryRun = true;
    if (f.ContainsKey("verbose")) o.Verbose = true;
    if (f.TryGetValue("guest-passwords", out var gp))
        o.GuestPasswords = gp.Split(',').Select(s => s).ToList();  // empty item = blank password
    if (f.TryGetValue("seeds", out var seeds))
        o.Seeds = seeds.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();
    if (f.TryGetValue("path-hash-size", out var phs)) o.PathHashSizeBytes = int.Parse(phs);
    if (f.ContainsKey("set-path-hash-mode")) o.SetPathHashMode = true;
    if (f.TryGetValue("deadline", out var dl)) o.Deadline = ParseDeadline(dl);

    if (string.IsNullOrWhiteSpace(o.Host)) throw new ArgumentException("no --host and no default companion host");
    if (o.RatePerMinute <= 0) throw new ArgumentException("--rate must be > 0");
    if (o.MaxDepth < 0) throw new ArgumentException("--depth must be >= 0");
    if (o.PathHashSizeBytes is < 1 or > 3) throw new ArgumentException("--path-hash-size must be 1, 2 or 3");
}

static DateTimeOffset ParseDeadline(string s)
{
    if (s.Contains('T') || s.Contains('-'))
        return DateTimeOffset.Parse(s, System.Globalization.CultureInfo.InvariantCulture);
    // "HH:mm" — today, or tomorrow if already past
    var t = TimeOnly.Parse(s, System.Globalization.CultureInfo.InvariantCulture);
    var now = DateTimeOffset.Now;
    var today = new DateTimeOffset(now.Year, now.Month, now.Day, t.Hour, t.Minute, 0, now.Offset);
    return today <= now ? today.AddDays(1) : today;
}
