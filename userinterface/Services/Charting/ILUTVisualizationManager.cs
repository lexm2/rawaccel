using SkiaSharp;
using userinterface.Charting.Core;
using userinterface.Charting.Controls;
using userinterface.Charting.Extensions;
using userspace_backend.Display;

namespace userinterface.Services.Charting;

public interface ILUTVisualizationManager
{
    void HandleChartClick(
        CartesianChart chart,
        LoggingScatterSeries<CurvePoint>? xLUTSeries,
        LoggingScatterSeries<CurvePoint>? yLUTSeries,
        double pixelX,
        double pixelY);

    ChartPoint? FindNearestLUTPoint(
        CartesianChart chart,
        LoggingScatterSeries<CurvePoint>? series,
        SKPoint clickPixels,
        double pixelTolerance = 100.0);

    void UpdateLUTSeriesVisibility(
        LoggingScatterSeries<CurvePoint>? xLUTSeries,
        LoggingScatterSeries<CurvePoint>? yLUTSeries,
        bool isLUT,
        bool hasYCurve);
}
