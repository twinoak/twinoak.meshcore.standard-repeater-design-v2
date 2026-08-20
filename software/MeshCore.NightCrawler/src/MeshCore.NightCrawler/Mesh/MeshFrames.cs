namespace MeshCore.NightCrawler.Mesh;

/// <summary>Our own companion node's identity (RESP_CODE_SELF_INFO).</summary>
public sealed record SelfInfo(
    string PublicKey,
    string Name,
    byte AdvType,
    double RadioFreqMHz,
    double RadioBwKHz,
    int RadioSf,
    int RadioCr);

/// <summary>
/// A contact the companion knows (from an advert it heard), as returned by
/// CMD_GET_CONTACTS / CMD_GET_CONTACT_BY_KEY. The out-path fields are what let us
/// build a reply path for anonymous requests.
/// </summary>
public sealed record ContactInfo(
    string PublicKey,          // 32-byte key, lowercase hex
    byte Type,                 // ADV_TYPE_*
    int OutPathLen,            // -1 = flood/unknown
    string OutPathHex,         // "" when flood/unknown
    int OutPathHashMode,       // -1 = flood
    string Name,
    double Lat,
    double Lon,
    uint LastAdvert)
{
    public bool HasDirectPath => OutPathLen >= 0;
    public string Role => OpCodes.RoleName(Type);
    public string ShortKey => PublicKey.Length >= 12 ? PublicKey[..12] : PublicKey;
}

/// <summary>Node scope config as read anonymously (ANON_REQ_TYPE_REGIONS).</summary>
public sealed record ScopeInfo(
    IReadOnlyList<string> FloodAllowedRegions,
    bool FloodsUnscoped,
    string Raw);

/// <summary>Owner/identity (+ firmware version when read via the guest path).</summary>
public sealed record OwnerInfo(
    string Name,
    string OwnerText,
    string? FirmwareVersion);

/// <summary>One entry of a repeater's neighbour table (REQ_TYPE_GET_NEIGHBOURS).</summary>
public sealed record NeighbourEntry(
    string PubKeyPrefix,   // hex, may be a prefix rather than the full key
    double SnrDb,
    int SecsAgo);

/// <summary>Result of a guest login attempt (never an admin login).</summary>
public sealed record GuestLoginResult(
    bool Success,
    byte Permissions,
    int FwVerLevel,
    int MatchedPasswordIndex)
{
    public static GuestLoginResult Failed { get; } = new(false, 0, 0, -1);

    public string Tier => !Success ? "none"
        : (Permissions & OpCodes.PermRoleMask) switch
        {
            OpCodes.PermGuest => "guest",
            OpCodes.PermReadOnly => "read-only",
            OpCodes.PermReadWrite => "read-write",
            OpCodes.PermAdmin => "admin",
            _ => "unknown",
        };
}
