using System.Threading.Tasks;
using userspace_backend.Display;

namespace userinterface.Services;

public interface IPreviewChartRenderer
{
    Task<byte[]> GenerateChartPreviewAsync(CurvePoint[] xPoints, CurvePoint[] yPoints, double yxRatio, string? xAxisName = null, string? yAxisName = null);

    string GenerateChartHash(CurvePoint[] xPoints, CurvePoint[] yPoints, double yxRatio);

    void ClearCache();

    void InvalidateCache(string cacheKey);
}
