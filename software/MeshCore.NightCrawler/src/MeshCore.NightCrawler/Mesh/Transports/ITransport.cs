namespace MeshCore.NightCrawler.Mesh.Transports;

/// <summary>
/// A framed byte transport to a MeshCore companion. Implementations own the
/// wire framing (the TCP/WiFi companion length-prefixes both directions), so the
/// client above works purely in terms of decoded frame payloads.
/// </summary>
public interface ITransport : IAsyncDisposable
{
    Task ConnectAsync(CancellationToken ct);

    /// <summary>Send one command frame payload (the first byte is the CMD code).</summary>
    Task SendFrameAsync(byte[] payload, CancellationToken ct);

    /// <summary>Receive one device→app frame payload (the first byte is the RESP/PUSH code).</summary>
    Task<byte[]> ReceiveFrameAsync(CancellationToken ct);

    bool IsConnected { get; }
}
