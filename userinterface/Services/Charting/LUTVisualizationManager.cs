using System;
using System.Collections.Generic;
using System.Linq;
using SkiaSharp;
using userinterface.Charting.Core;
using userinterface.Charting.Controls;
using userinterface.Charting.Extensions;
using userspace_backend.Display;

namespace userinterface.Services.Charting;

public class LUTVisualizationManager : ILUTVisualizationManager
{
    private const double DefaultPixelTolerance = 100.0;

    public void HandleChartClick(
        CartesianChart chart,
        LoggingScatterSeries<CurvePoint>? xLUTSeries,
        LoggingScatterSeries<CurvePoint>? yLUTSeries,
        double pixelX,
        double pixelY)
    {
        if (xLUTSeries == null || yLUTSeries == null || !xLUTSeries.IsVisible)
            return;

        var clickPoint = new SKPoint((float)pixelX, (float)pixelY);

        CheckLUTPointHitsPixelBased(chart, xLUTSeries, clickPoint);

        if (yLUTSeries.IsVisible)
        {
            CheckLUTPointHitsPixelBased(chart, yLUTSeries, clickPoint);
        }
    }

    public ChartPoint? FindNearestLUTPoint(
        CartesianChart chart,
        LoggingScatterSeries<CurvePoint>? series,
        SKPoint clickPixels,
        double pixelTolerance = DefaultPixelTolerance)
    {
        if (series?.Values is not IEnumerable<CurvePoint> points || !series.IsVisible)
            return null;

        ChartPoint? nearestPoint = null;
        double nearestDistance = double.MaxValue;

        foreach (var point in points)
        {
            var pointDataCoord = new ChartPoint(point.MouseSpeed, point.Output);
            var pointPixels = chart.ScaleDataToPixels(pointDataCoord);

            var pixelDistance = Math.Sqrt(
                Math.Pow(clickPixels.X - pointPixels.X, 2) +
                Math.Pow(clickPixels.Y - pointPixels.Y, 2)
            );

            if (pixelDistance <= pixelTolerance && pixelDistance < nearestDistance)
            {
                nearestDistance = pixelDistance;
                nearestPoint = pointDataCoord;
            }
        }

        return nearestPoint;
    }

    public void UpdateLUTSeriesVisibility(
        LoggingScatterSeries<CurvePoint>? xLUTSeries,
        LoggingScatterSeries<CurvePoint>? yLUTSeries,
        bool isLUT,
        bool hasYCurve)
    {
        if (xLUTSeries == null || yLUTSeries == null)
            return;

        xLUTSeries.IsVisible = isLUT;
        yLUTSeries.IsVisible = isLUT && hasYCurve;
    }

    private void CheckLUTPointHitsPixelBased(
        CartesianChart chart,
        LoggingScatterSeries<CurvePoint>? series,
        SKPoint clickPixels)
    {
        if (series?.Values is not IEnumerable<CurvePoint> points || !series.IsVisible)
            return;

        var pointList = points.ToList();
        for (int i = 0; i < pointList.Count; i++)
        {
            var point = pointList[i];
            var pointDataCoord = new ChartPoint(point.MouseSpeed, point.Output);
            var pointPixels = chart.ScaleDataToPixels(pointDataCoord);

            var pixelDistance = Math.Sqrt(
                Math.Pow(clickPixels.X - pointPixels.X, 2) +
                Math.Pow(clickPixels.Y - pointPixels.Y, 2)
            );

            if (pixelDistance <= DefaultPixelTolerance)
            {
                series.LogPointClick(pointDataCoord);
            }
        }
    }
}
