using System.Text;

namespace MeshCore.NightCrawler.Mesh;

/// <summary>
/// A tiny forward-only little-endian reader over a byte buffer. All MeshCore
/// companion frames are little-endian (the one documented exception, CayenneLPP
/// telemetry, is not parsed here). Reads past the end return zero/empty rather
/// than throwing, so a short/truncated frame degrades to sentinel values instead
/// of crashing the crawl.
/// </summary>
public struct ByteCursor
{
    private readonly byte[] _buf;
    private int _pos;

    public ByteCursor(byte[] buf, int start = 0)
    {
        _buf = buf;
        _pos = start;
    }

    public int Position => _pos;
    public int Remaining => Math.Max(0, _buf.Length - _pos);

    public byte U8()
    {
        if (_pos >= _buf.Length) return 0;
        return _buf[_pos++];
    }

    public ushort U16()
    {
        int v = U8() | (U8() << 8);
        return (ushort)v;
    }

    public short I16() => unchecked((short)U16());

    public uint U32()
    {
        uint v = 0;
        for (int i = 0; i < 4; i++) v |= (uint)U8() << (8 * i);
        return v;
    }

    public int I32() => unchecked((int)U32());

    public sbyte I8() => unchecked((sbyte)U8());

    public byte[] Bytes(int n)
    {
        if (n <= 0) return Array.Empty<byte>();
        int take = Math.Min(n, Remaining);
        var outBuf = new byte[take];
        Array.Copy(_buf, _pos, outBuf, 0, take);
        _pos += take;
        return outBuf;
    }

    public string Hex(int n) => Convert.ToHexString(Bytes(n)).ToLowerInvariant();

    /// <summary>UTF-8 string of exactly n bytes, trimming trailing NULs.</summary>
    public string FixedString(int n) => Encoding.UTF8.GetString(Bytes(n)).TrimEnd('\0');

    /// <summary>UTF-8 string of everything remaining, trimming trailing NULs.</summary>
    public string RestString() => Encoding.UTF8.GetString(Bytes(Remaining)).TrimEnd('\0');
}
