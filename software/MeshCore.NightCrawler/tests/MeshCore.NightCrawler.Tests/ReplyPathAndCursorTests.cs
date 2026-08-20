using MeshCore.NightCrawler.Mesh;
using MeshCore.NightCrawler.RateLimiting;
using Xunit;

namespace MeshCore.NightCrawler.Tests;

public class ReplyPathTests
{
    [Fact]
    public void Flood_or_unknown_path_encodes_zero_hop()
    {
        Assert.Equal(new byte[] { 0x00 }, ReplyPath.Encode(-1, "", -1));
        Assert.Equal(new byte[] { 0x00 }, ReplyPath.ZeroHop());
    }

    [Fact]
    public void Single_byte_hops_are_reversed_by_hop()
    {
        // out path aa,bb (mode 0, 1 byte/hop) → reply visits bb then aa; header = hops=2.
        Assert.Equal(new byte[] { 0x02, 0xbb, 0xaa }, ReplyPath.Encode(2, "aabb", 0));
    }

    [Fact]
    public void Multi_byte_hops_keep_hash_intact_and_pack_mode_in_header()
    {
        // mode 1 → 2 bytes/hop. Path aabb,ccdd → reversed by hop → ccdd,aabb.
        // header = hops(2) | (mode 1 << 6) = 0x42.
        Assert.Equal(new byte[] { 0x42, 0xcc, 0xdd, 0xaa, 0xbb }, ReplyPath.Encode(2, "aabbccdd", 1));
    }

    [Fact]
    public void Unsupported_hash_mode_falls_back_to_zero_hop()
    {
        Assert.Equal(new byte[] { 0x00 }, ReplyPath.Encode(3, "aabbccdd", 3));
    }
}

public class ByteCursorTests
{
    [Fact]
    public void Reads_little_endian_and_hex()
    {
        var c = new ByteCursor(new byte[] { 0x01, 0x02, 0x03, 0x00, 0x00, 0x01, 0xaa, 0xbb });
        Assert.Equal((ushort)0x0201, c.U16());
        Assert.Equal((uint)0x01000003, c.U32());
        Assert.Equal("aabb", c.Hex(2));
    }

    [Fact]
    public void Overread_returns_zero_and_empty()
    {
        var c = new ByteCursor(new byte[] { 0x05 });
        Assert.Equal((byte)5, c.U8());
        Assert.Equal((byte)0, c.U8());    // past the end
        Assert.Equal((uint)0, c.U32());
        Assert.Equal("", c.Hex(4));
    }
}

public class RateLimiterTests
{
    [Fact]
    public async Task Paces_requests_and_counts_them()
    {
        var clock = new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);
        Func<DateTimeOffset> now = () => clock;
        Func<TimeSpan, CancellationToken, Task> delay = (d, _) => { clock = clock.Add(d); return Task.CompletedTask; };

        // 60/min = 1/sec, no bursting.
        var lim = new TokenBucketRateLimiter(60, 1, now, delay);
        await lim.AcquireAsync(default);   // immediate (bucket starts full)
        await lim.AcquireAsync(default);   // waits 1s
        await lim.AcquireAsync(default);   // waits 1s

        Assert.Equal(3, (int)lim.Acquired);
        Assert.Equal(2.0, lim.TotalWait.TotalSeconds, 3);
        Assert.Equal(2.0, (clock - new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero)).TotalSeconds, 3);
    }

    [Fact]
    public async Task Burst_allows_a_run_then_throttles()
    {
        var clock = new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);
        Func<DateTimeOffset> now = () => clock;
        Func<TimeSpan, CancellationToken, Task> delay = (d, _) => { clock = clock.Add(d); return Task.CompletedTask; };

        var lim = new TokenBucketRateLimiter(60, burst: 3, now, delay);
        for (int i = 0; i < 3; i++) await lim.AcquireAsync(default);  // burst, no wait
        Assert.Equal(TimeSpan.Zero, lim.TotalWait);
        await lim.AcquireAsync(default);                              // 4th waits ~1s
        Assert.Equal(1.0, lim.TotalWait.TotalSeconds, 3);
    }
}
