using System.Text;
using MeshCore.NightCrawler.Mesh.Transports;
using MeshCore.NightCrawler.RateLimiting;

namespace MeshCore.NightCrawler.Mesh;

/// <summary>
/// Talks the MeshCore Companion Radio Protocol over a transport. Deliberately
/// linear: because the crawl is serialised (one request at a time, paced by the
/// rate limiter) there is exactly one reader, so every request is just
/// "send, then read frames until the reply I expect arrives" — no background
/// read loop, no TaskCompletionSource plumbing. Unsolicited adverts that arrive
/// mid-wait are folded out through <see cref="NewContactHeard"/> and skipped.
///
/// Wire formats verified against firmware v1.17.1 and meshcore_py.
/// </summary>
public sealed class CompanionClient : IMeshClient
{
    private readonly ITransport _transport;
    private readonly IRateLimiter _limiter;
    private readonly TimeSpan _replyTimeout;
    private readonly TimeSpan _ackTimeout = TimeSpan.FromSeconds(8);
    private readonly Action<string>? _log;
    private readonly bool _verbose;

    public SelfInfo? Self { get; private set; }

    /// <summary>The companion's configured path-hash mode (0,1,2 → 1,2,3 bytes), or null if unknown.</summary>
    public int? CompanionPathHashMode { get; private set; }

    /// <summary>Raised when a fresh advert (PUSH_CODE_NEW_ADVERT) is heard mid-crawl.</summary>
    public event Action<ContactInfo>? NewContactHeard;

    public CompanionClient(
        ITransport transport,
        IRateLimiter limiter,
        TimeSpan replyTimeout,
        Action<string>? log = null,
        bool verbose = false)
    {
        _transport = transport;
        _limiter = limiter;
        _replyTimeout = replyTimeout;
        _log = log;
        _verbose = verbose;
    }

    // ---------------------------------------------------------------- handshake

    public async Task ConnectAndHandshakeAsync(CancellationToken ct)
    {
        await _transport.ConnectAsync(ct);

        // CMD_APP_START: 0x01, app-proto-ver 0x03, 6 reserved bytes, app name.
        var appStart = new byte[] { OpCodes.CmdAppStart, 0x03 }
            .Concat(Encoding.ASCII.GetBytes("      ncrwl")).ToArray();
        await _transport.SendFrameAsync(appStart, ct);
        var self = await ReceiveUntilAsync(f => f.Code == OpCodes.RespSelfInfo, _ackTimeout, ct);
        Self = ParseSelfInfo(self.Body);
        Log($"companion: '{Self.Name}' key={Self.PublicKey[..12]} " +
            $"{Self.RadioFreqMHz:0.000} MHz BW{Self.RadioBwKHz:0.#} SF{Self.RadioSf} CR{Self.RadioCr}");

        // CMD_DEVICE_QUERY: 0x16, app-proto-ver 0x03 — read but not required.
        try
        {
            await _transport.SendFrameAsync(new byte[] { OpCodes.CmdDeviceQuery, 0x03 }, ct);
            var dev = await ReceiveUntilAsync(f => f.Code == OpCodes.RespDeviceInfo, _ackTimeout, ct);
            CompanionPathHashMode = ParseDeviceInfoPathHashMode(dev.Body);
            if (dev.Body.Length >= 1)
                Log($"companion firmware ver code: {dev.Body[0]}" +
                    (CompanionPathHashMode is { } m ? $"; path-hash {m + 1}-byte (mode {m})" : ""));
        }
        catch (TimeoutException) { /* older firmware may not answer; non-fatal */ }
    }

    /// <summary>Set the companion's path-hash mode (0,1,2 → 1,2,3 bytes). A device-settings change.</summary>
    public async Task SetPathHashModeAsync(int mode, CancellationToken ct)
    {
        mode = Math.Clamp(mode, 0, 2);
        await _transport.SendFrameAsync(new byte[] { OpCodes.CmdSetPathHashMode, 0x00, (byte)mode }, ct);
        try { await ReceiveUntilAsync(f => f.Code == OpCodes.RespOk || f.Code == OpCodes.RespError, _ackTimeout, ct); }
        catch (TimeoutException) { }
        CompanionPathHashMode = mode;
    }

    /// <summary>DEVICE_INFO layout: fw_ver(1), [ver>=3: 78 bytes], [ver>=9: repeat(1)], [ver>=10: path_hash_mode(1)].</summary>
    private static int? ParseDeviceInfoPathHashMode(byte[] body)
    {
        if (body.Length < 1) return null;
        int fwVer = body[0];
        if (fwVer < 10) return null;
        const int offset = 1 + 78 + 1;   // fw_ver + [max_contacts..ver block] + [repeat]
        return body.Length > offset ? body[offset] : (int?)null;
    }

    // ---------------------------------------------------------------- contacts (local)

    public async Task<IReadOnlyList<ContactInfo>> GetContactsAsync(CancellationToken ct)
    {
        await _transport.SendFrameAsync(new byte[] { OpCodes.CmdGetContacts }, ct);

        var contacts = new List<ContactInfo>();
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        cts.CancelAfter(_ackTimeout);
        while (true)
        {
            Frame f;
            try { f = await ReadFrameAsync(cts.Token); }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested) { break; }

            if (f.Code == OpCodes.RespContact) contacts.Add(ParseContact(f.Body));
            else if (f.Code == OpCodes.RespContactsEnd || f.Code == OpCodes.RespError) break;
            else if (f.Code == OpCodes.PushNewAdvert) RaiseAdvert(f.Body);
            // RespContactsStart and anything else: ignore and keep reading.
        }
        return contacts;
    }

    public async Task<ContactInfo?> RefreshContactAsync(string pubKeyHex, CancellationToken ct)
    {
        var key = Convert.FromHexString(pubKeyHex);
        var frame = new byte[] { OpCodes.CmdGetContactByKey }.Concat(key).ToArray();
        await _transport.SendFrameAsync(frame, ct);
        try
        {
            var f = await ReceiveUntilAsync(
                x => x.Code == OpCodes.RespContact || x.Code == OpCodes.RespError, _ackTimeout, ct);
            return f.Code == OpCodes.RespContact ? ParseContact(f.Body) : null;
        }
        catch (TimeoutException) { return null; }
    }

    // ---------------------------------------------------------------- guest login (OTA)

    public async Task<GuestLoginResult> GuestLoginAsync(
        ContactInfo node, IReadOnlyList<string> candidatePasswords, CancellationToken ct)
    {
        var key = Convert.FromHexString(node.PublicKey);
        string prefix = node.PublicKey[..12];

        for (int i = 0; i < candidatePasswords.Count; i++)
        {
            await _limiter.AcquireAsync(ct);
            var frame = new byte[] { OpCodes.CmdSendLogin }
                .Concat(key)
                .Concat(Encoding.UTF8.GetBytes(candidatePasswords[i]))
                .ToArray();
            await _transport.SendFrameAsync(frame, ct);

            try
            {
                var f = await ReceiveUntilAsync(
                    x => (x.Code == OpCodes.PushLoginSuccess || x.Code == OpCodes.PushLoginFailed)
                         && LoginPrefixMatches(x, prefix),
                    _replyTimeout, ct);

                if (f.Code == OpCodes.PushLoginSuccess)
                    return ParseLoginSuccess(f.Body, i);
                // explicit failure → try the next candidate
            }
            catch (TimeoutException)
            {
                // No answer at all → node is unreachable/offline; further password
                // guesses would just burn airtime. Stop here.
                Log($"  login: no response from {prefix} (unreachable)");
                return GuestLoginResult.Failed;
            }
        }
        return GuestLoginResult.Failed;
    }

    // ---------------------------------------------------------------- guest binary reads (OTA)

    public async Task<IReadOnlyList<NeighbourEntry>?> GetNeighboursAsync(ContactInfo node, CancellationToken ct)
    {
        // Ask for FULL public keys so every neighbour is directly addressable (a node
        // must be a contact before it can be queried, and we can only make it one if we
        // hold its full key). A full-key entry is 37 bytes and the reply buffer is 130,
        // so ~3 fit per page — page through by offset until we have them all.
        const byte prefixLen = 32;
        const byte pageSize = 3;
        var all = new List<NeighbourEntry>();
        int offset = 0, total = int.MaxValue, guard = 0;

        while (offset < total && guard++ < 64)
        {
            var rnd = new byte[4];
            Random.Shared.NextBytes(rnd);
            // params after the request-type byte: version, count, offsetLo, offsetHi, orderBy, prefixLen, rnd[4]
            var data = new byte[] { 0x00, pageSize, (byte)(offset & 0xFF), (byte)((offset >> 8) & 0xFF), 0x00, prefixLen }
                .Concat(rnd).ToArray();

            var resp = await BinaryRequestAsync(node.PublicKey, OpCodes.ReqTypeGetNeighbours, data, ct);
            if (resp is null)
                return all.Count > 0 ? all : null;   // first page failed → null; later page → keep what we have

            var (pageTotal, page) = ParseNeighbours(resp, prefixLen);
            total = pageTotal;
            if (page.Count == 0) break;
            all.AddRange(page);
            offset += page.Count;
        }
        return all;
    }

    /// <summary>
    /// Add (or update) a node as a contact on the companion so it can be logged in to
    /// and queried. Local command (no airtime). Uses a flood out-path (0xFF) so the
    /// first login/request floods and reaches a multi-hop node, after which the
    /// companion learns a direct path.
    /// </summary>
    public async Task EnsureContactAsync(string pubKeyHex, string name, byte type, CancellationToken ct)
    {
        await _transport.SendFrameAsync(BuildAddContactFrame(pubKeyHex, name, type), ct);
        try { await ReceiveUntilAsync(f => f.Code == OpCodes.RespOk || f.Code == OpCodes.RespError, _ackTimeout, ct); }
        catch (TimeoutException) { }
    }

    private static byte[] BuildAddContactFrame(string pubKeyHex, string name, byte type)
    {
        // CMD_ADD_UPDATE_CONTACT: pubkey(32), type(1), flags(1), out_path_len(1)=0xFF flood,
        //                         out_path(64), adv_name(32), last_advert(4), lat(4), lon(4)
        var frame = new List<byte>(1 + 143) { OpCodes.CmdAddUpdateContact };
        frame.AddRange(Convert.FromHexString(pubKeyHex));  // 32
        frame.Add(type);
        frame.Add(0);                                      // flags
        frame.Add(0xFF);                                   // out_path_len = flood/unknown
        frame.AddRange(new byte[64]);                      // out_path (empty)
        var nameField = new byte[32];
        var nb = Encoding.UTF8.GetBytes(name ?? string.Empty);
        Buffer.BlockCopy(nb, 0, nameField, 0, Math.Min(nb.Length, 32));
        frame.AddRange(nameField);
        frame.AddRange(new byte[4]);                       // last_advert
        frame.AddRange(new byte[4]);                       // adv_lat
        frame.AddRange(new byte[4]);                       // adv_lon
        return frame.ToArray();
    }

    public async Task<OwnerInfo?> GetOwnerInfoAsync(ContactInfo node, CancellationToken ct)
    {
        var resp = await BinaryRequestAsync(node.PublicKey, OpCodes.ReqTypeGetOwnerInfo, Array.Empty<byte>(), ct);
        if (resp is null) return null;
        // "FIRMWARE_VERSION\nnode_name\nowner_info"
        var parts = Encoding.UTF8.GetString(resp).TrimEnd('\0').Split('\n');
        string ver = parts.Length > 0 ? parts[0] : "";
        string name = parts.Length > 1 ? parts[1] : "";
        string owner = parts.Length > 2 ? string.Join('\n', parts[2..]) : "";
        return new OwnerInfo(name, owner, string.IsNullOrWhiteSpace(ver) ? null : ver);
    }

    // ---------------------------------------------------------------- anonymous reads (OTA)

    public async Task<OwnerInfo?> GetOwnerAnonAsync(ContactInfo node, CancellationToken ct)
    {
        var resp = await AnonRequestAsync(node, OpCodes.AnonReqOwner, ct);
        if (resp is null || resp.Length < 4) return null;
        // [node_clock:4]["name\nowner_info"]
        var text = Encoding.UTF8.GetString(resp, 4, resp.Length - 4).TrimEnd('\0');
        var parts = text.Split('\n');
        string name = parts.Length > 0 ? parts[0] : "";
        string owner = parts.Length > 1 ? string.Join('\n', parts[1..]) : "";
        return new OwnerInfo(name, owner, null);
    }

    public async Task<ScopeInfo?> GetScopesAsync(ContactInfo node, CancellationToken ct)
    {
        var resp = await AnonRequestAsync(node, OpCodes.AnonReqRegions, ct);
        if (resp is null || resp.Length < 4) return null;
        // [node_clock:4][comma-separated flood-allowed region names]
        string raw = Encoding.UTF8.GetString(resp, 4, resp.Length - 4).TrimEnd('\0');
        var names = raw.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        bool floodsUnscoped = names.Contains("*");
        var regions = names.Where(n => n != "*").ToArray();
        return new ScopeInfo(regions, floodsUnscoped, raw);
    }

    // ---------------------------------------------------------------- request plumbing

    private async Task<byte[]?> BinaryRequestAsync(string pubKeyHex, byte reqType, byte[] data, CancellationToken ct)
    {
        await _limiter.AcquireAsync(ct);
        var key = Convert.FromHexString(pubKeyHex);
        var frame = new byte[] { OpCodes.CmdSendBinaryReq }
            .Concat(key).Append(reqType).Concat(data).ToArray();
        return await SendAndAwaitBinaryAsync(frame, ct);
    }

    private async Task<byte[]?> AnonRequestAsync(ContactInfo node, byte anonType, CancellationToken ct)
    {
        await _limiter.AcquireAsync(ct);
        var key = Convert.FromHexString(node.PublicKey);
        // The request body is the reply path the server should answer along. A node
        // with a known direct path gets that path (reversed); otherwise zero-hop,
        // which only reaches a direct RF neighbour (multi-hop needs path discovery — v0.2).
        byte[] replyPath = node.HasDirectPath
            ? ReplyPath.Encode(node.OutPathLen, node.OutPathHex, node.OutPathHashMode)
            : ReplyPath.ZeroHop();
        var frame = new byte[] { OpCodes.CmdSendAnonReq }
            .Concat(key).Append(anonType).Concat(replyPath).ToArray();
        return await SendAndAwaitBinaryAsync(frame, ct);
    }

    /// <summary>
    /// Send a request frame, capture the tag from RESP_CODE_SENT, then wait for the
    /// matching PUSH_CODE_BINARY_RESPONSE and return its response_data (past the tag).
    /// Returns null on device error or timeout.
    /// </summary>
    private async Task<byte[]?> SendAndAwaitBinaryAsync(byte[] frame, CancellationToken ct)
    {
        await _transport.SendFrameAsync(frame, ct);
        try
        {
            var sent = await ReceiveUntilAsync(
                f => f.Code == OpCodes.RespSent || f.Code == OpCodes.RespError, _ackTimeout, ct);
            if (sent.Code == OpCodes.RespError) return null;

            // RESP_CODE_SENT body: type(1), expected_ack(4)=tag, suggested_timeout(4)
            var tag = sent.Body.Skip(1).Take(4).ToArray();

            var resp = await ReceiveUntilAsync(
                f => f.Code == OpCodes.PushBinaryResponse && BinaryTagMatches(f.Body, tag),
                _replyTimeout, ct);

            // BINARY_RESPONSE body: reserved(1), tag(4), response_data
            return resp.Body.Skip(5).ToArray();
        }
        catch (TimeoutException) { return null; }
    }

    // ---------------------------------------------------------------- frame IO

    private readonly record struct Frame(byte Code, byte[] Body);

    private async Task<Frame> ReadFrameAsync(CancellationToken ct)
    {
        var payload = await _transport.ReceiveFrameAsync(ct);
        if (payload.Length == 0) return new Frame(0, Array.Empty<byte>());
        var body = new byte[payload.Length - 1];
        Buffer.BlockCopy(payload, 1, body, 0, body.Length);
        if (_verbose) Log($"    rx code=0x{payload[0]:x2} len={body.Length}");
        return new Frame(payload[0], body);
    }

    private async Task<Frame> ReceiveUntilAsync(Func<Frame, bool> match, TimeSpan timeout, CancellationToken ct)
    {
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        cts.CancelAfter(timeout);
        while (true)
        {
            Frame f;
            try { f = await ReadFrameAsync(cts.Token); }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested)
            {
                throw new TimeoutException("timed out waiting for a companion frame");
            }

            if (match(f)) return f;

            // Fold fresh adverts into discovery; ignore every other unsolicited frame.
            if (f.Code == OpCodes.PushNewAdvert) RaiseAdvert(f.Body);
        }
    }

    private void RaiseAdvert(byte[] body)
    {
        try { NewContactHeard?.Invoke(ParseContact(body)); }
        catch { /* a malformed advert never disturbs the crawl */ }
    }

    private static bool BinaryTagMatches(byte[] binaryRespBody, byte[] tag)
        => binaryRespBody.Length >= 5 && binaryRespBody.AsSpan(1, 4).SequenceEqual(tag);

    private static bool LoginPrefixMatches(Frame f, string prefix12)
    {
        // LOGIN_SUCCESS body: perms(1), pubkey_prefix(6)...; LOGIN_FAILED: reserved(1), pubkey_prefix(6)
        if (f.Body.Length < 7) return true; // too short to check — accept
        string got = Convert.ToHexString(f.Body, 1, 6).ToLowerInvariant();
        return got == prefix12;
    }

    // ---------------------------------------------------------------- parsers

    private static SelfInfo ParseSelfInfo(byte[] body)
    {
        var c = new ByteCursor(body);
        byte advType = c.U8();
        c.U8(); c.U8();                    // tx_power, max_tx_power
        string key = c.Hex(32);
        c.I32(); c.I32();                  // lat, lon
        c.U8(); c.U8(); c.U8(); c.U8();    // multi_acks, adv_loc_policy, telemetry_mode, manual_add
        double freq = c.U32() / 1000.0;
        double bw = c.U32() / 1000.0;
        int sf = c.U8();
        int cr = c.U8();
        string name = c.RestString();
        return new SelfInfo(key, name, advType, freq, bw, sf, cr);
    }

    private static ContactInfo ParseContact(byte[] body)
    {
        var c = new ByteCursor(body);
        string key = c.Hex(32);
        byte type = c.U8();
        c.U8();                            // flags
        byte plen = c.U8();
        int outPathLen, hashMode;
        if (plen == 255) { outPathLen = -1; hashMode = -1; }
        else { hashMode = plen >> 6; outPathLen = plen & 0x3F; }
        byte[] pathBytes = c.Bytes(64);    // fixed 64-byte field
        string outPath = outPathLen > 0
            ? Convert.ToHexString(pathBytes, 0, Math.Min(pathBytes.Length, outPathLen * (hashMode + 1))).ToLowerInvariant()
            : "";
        string name = c.FixedString(32);
        uint lastAdvert = c.U32();
        double lat = c.I32() / 1e6;
        double lon = c.I32() / 1e6;
        c.U32();                           // lastmod
        return new ContactInfo(key, type, outPathLen, outPath, hashMode, name, lat, lon, lastAdvert);
    }

    private static GuestLoginResult ParseLoginSuccess(byte[] body, int matchedIndex)
    {
        var c = new ByteCursor(body);
        byte perms = body.Length >= 1 ? c.U8() : (byte)0;
        if (body.Length >= 7) c.Bytes(6);  // pubkey prefix
        if (body.Length >= 11) c.U32();    // server_timestamp
        if (body.Length >= 12) c.U8();     // acl_permissions
        int fwVer = body.Length >= 13 ? c.U8() : 0;
        return new GuestLoginResult(true, perms, fwVer, matchedIndex);
    }

    private static (int Total, List<NeighbourEntry> Entries) ParseNeighbours(byte[] data, int prefixLen)
    {
        var c = new ByteCursor(data);
        int total = c.I16();               // how many the node knows in total (for paging)
        int returned = c.I16();
        var list = new List<NeighbourEntry>(Math.Max(0, returned));
        for (int i = 0; i < returned; i++)
        {
            string pk = c.Hex(prefixLen);
            int secsAgo = c.I32();
            double snr = c.I8() / 4.0;
            if (pk.Length == 0) break;
            list.Add(new NeighbourEntry(pk, snr, secsAgo));
        }
        return (Math.Max(total, list.Count), list);
    }

    private void Log(string msg) => _log?.Invoke(msg);

    public async ValueTask DisposeAsync() => await _transport.DisposeAsync();
}
