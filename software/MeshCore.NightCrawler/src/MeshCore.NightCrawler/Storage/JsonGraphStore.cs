using System.Text.Json;
using System.Text.Json.Serialization;
using MeshCore.NightCrawler.Model;

namespace MeshCore.NightCrawler.Storage;

/// <summary>
/// Atomic, incremental JSON persistence. Writes to a temp file then File.Move's
/// over the target, so a crash mid-write never corrupts the graph.
/// </summary>
public sealed class JsonGraphStore : IGraphStore
{
    private readonly string _path;

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    public JsonGraphStore(string path) => _path = path;

    public async Task<MeshGraph> LoadAsync(CancellationToken ct)
    {
        if (!File.Exists(_path)) return new MeshGraph();
        await using var fs = File.OpenRead(_path);
        var graph = await JsonSerializer.DeserializeAsync<MeshGraph>(fs, Options, ct);
        return graph ?? new MeshGraph();
    }

    public async Task SaveAsync(MeshGraph graph, CancellationToken ct)
    {
        graph.GeneratedAt = DateTimeOffset.UtcNow;
        var dir = Path.GetDirectoryName(Path.GetFullPath(_path));
        if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);

        var tmp = _path + ".tmp";
        await using (var fs = File.Create(tmp))
        {
            await JsonSerializer.SerializeAsync(fs, graph, Options, ct);
        }
        // File.Move with overwrite is atomic on the same volume.
        File.Move(tmp, _path, overwrite: true);
    }
}
