using System.Text;
using MeshCore.NightCrawler.Mesh;
using MeshCore.NightCrawler.Mesh.Transports;
using MeshCore.NightCrawler.RateLimiting;
using Xunit;

namespace MeshCore.NightCrawler.Tests;

/// <summary>
/// Drives the real CompanionClient against a scripted transport whose inbound
/// frames are built by hand to match the firmware wire format. This validates the
/// actual parse/round-trip logic (handshake, anon scopes, guest neighbours)
/// without a radio.
/// </summary>
public class CompanionClientTests
{
    private sealed class ScriptedTransport : ITransport
    {
        private readonly Queue<byte[]> _inbound;
        public List<byte[]> Sent { get; } = new();
        public bool IsConnected { get; private set; }

        public ScriptedTransport(IEnumerable<byte[]> inbound) => _inbound = new Queue<byte[]>(inbound);
        public Task ConnectAsync(CancellationToken ct) { IsConnected = true; return Task.CompletedTask; }
        public Task SendFrameAsync(byte[] payload, CancellationToken ct) { Sent.Add(payload); return Task.CompletedTask; }
        public Task<byte[]> ReceiveFrameAsync(CancellationToken ct)
            => _inbound.Count > 0 ? Task.FromResult(_inbound.Dequeue()) : throw new IOException("scripted frames exhausted");
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }

    private static byte[] U16(int v) => new[] { (byte)(v & 0xff), (byte)((v >> 8) & 0xff) };
    private static byte[] U32(long v) => new[] { (byte)(v & 0xff), (byte)((v >> 8) & 0xff), (byte)((v >> 16) & 0xff), (byte)((v >> 24) & 0xff) };
    private static byte[] Cat(params byte[][] p) => p.SelectMany(x => x).ToArray();
    private static byte[] Frame(byte code, byte[] body) => Cat(new[] { code }, body);

    private static byte[] SelfInfoFrame() => Frame(OpCodes.RespSelfInfo, Cat(
        new byte[] { OpCodes.AdvTypeChat, 0, 0 },       // adv_type, tx_power, max_tx_power
        new byte[32],                                    // public key
        U32(0), U32(0),                                  // lat, lon
        new byte[] { 0, 0, 0, 0 },                       // multi_acks, adv_loc, telemetry, manual
        U32(869618), U32(62500),                         // freq (kHz), bw (Hz/1000)
        new byte[] { 8, 8 },                             // sf, cr
        Encoding.UTF8.GetBytes("companion")));

    private static readonly byte[] Tag1 = { 1, 2, 3, 4 };
    private static readonly byte[] Tag2 = { 5, 6, 7, 8 };

    private static CompanionClient NewClient(ScriptedTransport t) =>
        new(t, new TokenBucketRateLimiter(1_000_000, 1_000_000, delay: (_, _) => Task.CompletedTask),
            TimeSpan.FromSeconds(5));

    [Fact]
    public async Task Handshake_parses_self_info()
    {
        var t = new ScriptedTransport(new[]
        {
            SelfInfoFrame(),
            Frame(OpCodes.RespDeviceInfo, new byte[] { 13 }),
        });
        await using var client = NewClient(t);
        await client.ConnectAndHandshakeAsync(default);

        Assert.NotNull(client.Self);
        Assert.Equal("companion", client.Self!.Name);
        Assert.Equal(869.618, client.Self.RadioFreqMHz, 3);
        Assert.Equal(8, client.Self.RadioSf);
    }

    [Fact]
    public async Task Anon_scopes_round_trip_parses_flood_allowed_set()
    {
        var scopeResp = Frame(OpCodes.PushBinaryResponse, Cat(
            new byte[] { 0 }, Tag1,                       // reserved, tag
            U32(0), Encoding.UTF8.GetBytes("*,DK")));     // node_clock, names
        var t = new ScriptedTransport(new[]
        {
            SelfInfoFrame(),
            Frame(OpCodes.RespDeviceInfo, new byte[] { 13 }),
            Frame(OpCodes.RespSent, Cat(new byte[] { 0 }, Tag1, U32(4000))),
            scopeResp,
        });
        await using var client = NewClient(t);
        await client.ConnectAndHandshakeAsync(default);

        var node = new ContactInfo(new string('a', 64), OpCodes.AdvTypeRepeater, 0, "", 0, "n", 0, 0, 0);
        var scopes = await client.GetScopesAsync(node, default);

        Assert.NotNull(scopes);
        Assert.True(scopes!.FloodsUnscoped);
        Assert.Equal(new[] { "DK" }, scopes.FloodAllowedRegions);

        // The anon request frame is well-formed: CMD, 32-byte key, then the sub-type.
        var anon = t.Sent.Single(f => f[0] == OpCodes.CmdSendAnonReq);
        Assert.Equal(OpCodes.AnonReqRegions, anon[1 + 32]);
    }

    [Fact]
    public async Task Guest_neighbours_round_trip_parses_entries()
    {
        // The client now requests full 32-byte keys and pages; total=1 stops after one page.
        var key32 = Enumerable.Repeat((byte)0xAB, 32).ToArray();
        var neighResp = Frame(OpCodes.PushBinaryResponse, Cat(
            new byte[] { 0 }, Tag2,
            U16(1), U16(1),                               // total, returned
            key32, U32(60), new byte[] { 30 }));          // full pubkey, secs_ago, snr*4
        var t = new ScriptedTransport(new[]
        {
            SelfInfoFrame(),
            Frame(OpCodes.RespDeviceInfo, new byte[] { 13 }),
            Frame(OpCodes.RespSent, Cat(new byte[] { 0 }, Tag2, U32(4000))),
            neighResp,
        });
        await using var client = NewClient(t);
        await client.ConnectAndHandshakeAsync(default);

        var node = new ContactInfo(new string('a', 64), OpCodes.AdvTypeRepeater, 0, "", 0, "n", 0, 0, 0);
        var neigh = await client.GetNeighboursAsync(node, default);

        Assert.NotNull(neigh);
        var one = Assert.Single(neigh!);
        Assert.Equal(Convert.ToHexString(key32).ToLowerInvariant(), one.PubKeyPrefix);
        Assert.Equal(7.5, one.SnrDb, 3);
        Assert.Equal(60, one.SecsAgo);
    }
}
