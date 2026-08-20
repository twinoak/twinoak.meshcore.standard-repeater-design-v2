namespace MeshCore.NightCrawler.Mesh;

/// <summary>
/// Builds the reply-path a repeater should use when answering an anonymous
/// request. Ported from meshcore_py <c>encode_reply_path()</c>.
///
/// The leading byte packs two fields the server unpacks as:
///   reply_path_len       = byte &amp; 63
///   reply_path_hash_size = (byte &gt;&gt; 6) + 1
/// and the path itself is reversed by *hop* (each hop keeps its multi-byte hash
/// intact), because a return path visits the same hops in the opposite order.
/// </summary>
public static class ReplyPath
{
    private const int MaxReplyPathBytes = 64;  // server reply_path[64], memcpy'd with no bounds check
    private const int MaxReplyPathHops = 63;   // low 6 bits of the header byte

    /// <param name="outPathLen">hop count of the known out path (-1 = flood/unknown → zero-hop).</param>
    /// <param name="outPathHex">the out path bytes as hex, or empty.</param>
    /// <param name="outPathHashMode">0,1,2 (hash_size = mode+1); -1 = flood.</param>
    public static byte[] Encode(int outPathLen, string? outPathHex, int outPathHashMode)
    {
        int hashMode = Math.Max(outPathHashMode, 0);
        if (hashMode > 2)
            return new byte[] { 0x00 }; // 4-byte hops are rejected on the wire → ask for zero-hop

        int hashSize = hashMode + 1;
        int hops = Math.Clamp(outPathLen, 0, MaxReplyPathHops);

        byte[] raw = string.IsNullOrEmpty(outPathHex)
            ? Array.Empty<byte>()
            : Convert.FromHexString(outPathHex);

        // Never describe more hops than the raw bytes actually carry.
        hops = Math.Min(hops, raw.Length / hashSize);
        int maxHops = Math.Min(MaxReplyPathHops, MaxReplyPathBytes / hashSize);
        if (hops > maxHops) hops = maxHops;

        // Reverse by hop, keeping each hop's hash bytes intact.
        var path = new List<byte>(hops * hashSize);
        for (int i = hops - 1; i >= 0; i--)
            path.AddRange(raw.AsSpan(i * hashSize, hashSize).ToArray());

        var result = new byte[1 + path.Count];
        result[0] = (byte)(hops | (hashMode << 6));
        path.CopyTo(result, 1);
        return result;
    }

    /// <summary>Convenience: a zero-hop direct reply path (header byte 0x00).</summary>
    public static byte[] ZeroHop() => new byte[] { 0x00 };
}
