using System.Net.Sockets;

namespace MeshCore.NightCrawler.Mesh.Transports;

/// <summary>
/// TCP/WiFi companion transport. Framing (verified against the firmware's
/// SerialWifiInterface and meshcore_py tcp_cx):
///   app  → device:  0x3C  [u16 LE length]  [payload]
///   device → app:   0x3E  [u16 LE length]  [payload]
/// Leading junk before the 0x3E start marker is discarded (some radios interleave
/// console text on the same link).
/// </summary>
public sealed class TcpTransport : ITransport
{
    private readonly string _host;
    private readonly int _port;
    private TcpClient? _client;
    private NetworkStream? _stream;

    public TcpTransport(string host, int port)
    {
        _host = host;
        _port = port;
    }

    public bool IsConnected => _client?.Connected ?? false;

    public async Task ConnectAsync(CancellationToken ct)
    {
        _client = new TcpClient { NoDelay = true };
        await _client.ConnectAsync(_host, _port, ct);
        _stream = _client.GetStream();
    }

    public async Task SendFrameAsync(byte[] payload, CancellationToken ct)
    {
        if (_stream is null) throw new InvalidOperationException("transport not connected");
        int len = payload.Length;
        var frame = new byte[3 + len];
        frame[0] = 0x3C;
        frame[1] = (byte)(len & 0xFF);
        frame[2] = (byte)((len >> 8) & 0xFF);
        Buffer.BlockCopy(payload, 0, frame, 3, len);
        await _stream.WriteAsync(frame, ct);
        await _stream.FlushAsync(ct);
    }

    public async Task<byte[]> ReceiveFrameAsync(CancellationToken ct)
    {
        // Scan for the 0x3E start-of-frame marker, discarding anything before it.
        byte marker;
        do { marker = await ReadByteAsync(ct); } while (marker != 0x3E);

        var header = await ReadExactAsync(2, ct);
        int len = header[0] | (header[1] << 8);
        if (len < 1 || len > 4096)
            throw new IOException($"implausible companion frame length {len}");

        return await ReadExactAsync(len, ct);
    }

    private readonly byte[] _one = new byte[1];

    private async Task<byte> ReadByteAsync(CancellationToken ct)
    {
        int n = await _stream!.ReadAsync(_one.AsMemory(0, 1), ct);
        if (n == 0) throw new IOException("companion closed the connection");
        return _one[0];
    }

    private async Task<byte[]> ReadExactAsync(int count, CancellationToken ct)
    {
        var buf = new byte[count];
        int got = 0;
        while (got < count)
        {
            int n = await _stream!.ReadAsync(buf.AsMemory(got, count - got), ct);
            if (n == 0) throw new IOException("companion closed the connection");
            got += n;
        }
        return buf;
    }

    public ValueTask DisposeAsync()
    {
        _stream?.Dispose();
        _client?.Dispose();
        return ValueTask.CompletedTask;
    }
}
