namespace MeshCore.NightCrawler.Model;

/// <summary>Node lifecycle status, mirroring DATA-MODEL.md.</summary>
public static class NodeStatus
{
    public const string Crawled = "crawled";                 // scopes + neighbours + version
    public const string ScopeOnly = "scope-only";            // scopes read, not descended
    public const string GuestAuthFailed = "guest-auth-failed";// no candidate password worked
    public const string Partial = "partial";                 // some reads succeeded, some failed
    public const string Referenced = "referenced";           // known only as a neighbour/advert
    public const string Unreachable = "unreachable";         // no response at all
    public const string BeyondDepth = "beyond-depth";        // recorded but past the depth bound
}

public sealed class ScopeRecord
{
    public List<string> FloodAllowedRegions { get; set; } = new();
    public bool FloodsUnscoped { get; set; }
    public string Raw { get; set; } = "";
}

public sealed class NeighbourRecord
{
    public string PublicKey { get; set; } = "";   // usually a prefix
    public double SnrDb { get; set; }
    public int SecsAgo { get; set; }
    public DateTimeOffset LastHeard { get; set; }
}

public sealed class AccessRecord
{
    public bool AnonReadOk { get; set; }
    public bool GuestLoginAttempted { get; set; }
    public bool GuestLoginSucceeded { get; set; }
    public int GuestPasswordIndex { get; set; } = -1;  // which candidate matched (index, never the password)
    public string PermissionTier { get; set; } = "none";
    public bool ReachedOverAir { get; set; }
}

public sealed class MeshNode
{
    public string PublicKey { get; set; } = "";
    public List<string> AliasKeys { get; set; } = new();
    public string? Name { get; set; }
    public string Role { get; set; } = "unknown";
    public string? FirmwareVersion { get; set; }
    public string? OwnerInfo { get; set; }
    public double? Lat { get; set; }
    public double? Lon { get; set; }

    public ScopeRecord? Scopes { get; set; }
    public List<NeighbourRecord> Neighbours { get; set; } = new();
    public AccessRecord Access { get; set; } = new();

    public int Depth { get; set; }
    public string Status { get; set; } = NodeStatus.Referenced;

    public DateTimeOffset FirstSeen { get; set; }
    public DateTimeOffset LastSeen { get; set; }
    public DateTimeOffset? LastCrawlAttempt { get; set; }
    public DateTimeOffset? LastCrawled { get; set; }

    public string ShortKey => PublicKey.Length >= 12 ? PublicKey[..12] : PublicKey;
}
