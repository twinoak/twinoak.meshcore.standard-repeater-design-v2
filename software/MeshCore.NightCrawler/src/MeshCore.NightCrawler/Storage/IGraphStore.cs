using MeshCore.NightCrawler.Model;

namespace MeshCore.NightCrawler.Storage;

public interface IGraphStore
{
    Task<MeshGraph> LoadAsync(CancellationToken ct);
    Task SaveAsync(MeshGraph graph, CancellationToken ct);
}
