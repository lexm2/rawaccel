using System;
using System.Collections.Generic;
using System.Linq;
using SkiaSharp;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Rendering;

public class ChartRenderer
{
    private const float LeftMargin = 70f;
    private const float RightMargin = 20f;
    private const float TopMargin = 20f;
    private const float BottomMargin = 70f;

    private SKRect currentChartArea;
    private double currentXMin;
    private double currentXMax;
    private double currentYMin;
    private double currentYMax;

    public void Render(SKCanvas canvas, IEnumerable<ISeries>? series, IEnumerable<IAxis>? xAxes, IEnumerable<IAxis>? yAxes, SKRect bounds)
    {
        if (series == null || xAxes == null || yAxes == null)
            return;

        var seriesList = series.Where(s => s.IsVisible).ToList();
        if (seriesList.Count == 0)
            return;

        currentChartArea = CalculateChartArea(bounds);

        var xAxis = xAxes.FirstOrDefault();
        var yAxis = yAxes.FirstOrDefault();

        if (xAxis == null || yAxis == null)
            return;

        (currentXMin, currentXMax) = GetAxisRange(xAxis, seriesList, isX: true);
        (currentYMin, currentYMax) = GetAxisRange(yAxis, seriesList, isX: false);

        if (currentXMax <= currentXMin || currentYMax <= currentYMin)
            return;

        AxisRenderer.RenderVerticalAxis(canvas, yAxis, currentChartArea, currentYMin, currentYMax);
        AxisRenderer.RenderHorizontalAxis(canvas, xAxis, currentChartArea, currentXMin, currentXMax);

        foreach (var s in seriesList)
        {
            if (s is ILineSeries lineSeries)
                SeriesRenderer.RenderLineSeries(canvas, lineSeries, currentChartArea, currentXMin, currentXMax, currentYMin, currentYMax);
            else if (s is IScatterSeries scatterSeries)
                SeriesRenderer.RenderScatterSeries(canvas, scatterSeries, currentChartArea, currentXMin, currentXMax, currentYMin, currentYMax);
        }
    }

    public SKRect GetChartArea() => currentChartArea;

    public (double xMin, double xMax, double yMin, double yMax) GetRanges() =>
        (currentXMin, currentXMax, currentYMin, currentYMax);

    private SKRect CalculateChartArea(SKRect bounds)
    {
        return new SKRect(
            bounds.Left + LeftMargin,
            bounds.Top + TopMargin,
            bounds.Right - RightMargin,
            bounds.Bottom - BottomMargin
        );
    }

    private (double min, double max) GetAxisRange(IAxis axis, List<ISeries> seriesList, bool isX)
    {
        if (axis.MinLimit.HasValue && axis.MaxLimit.HasValue)
        {
            return (axis.MinLimit.Value, axis.MaxLimit.Value);
        }

        double dataMin = double.MaxValue;
        double dataMax = double.MinValue;

        foreach (var series in seriesList)
        {
            if (series.Values == null || series.Mapping == null)
                continue;

            int index = 0;
            foreach (var item in (System.Collections.IEnumerable)series.Values)
            {
                var point = series.Mapping(item, index++);
                var value = isX ? point.X : point.Y;

                if (value < dataMin)
                    dataMin = value;
                if (value > dataMax)
                    dataMax = value;
            }
        }

        if (dataMin == double.MaxValue || dataMax == double.MinValue)
        {
            dataMin = 0;
            dataMax = 100;
        }

        if (dataMax <= dataMin)
        {
            dataMax = dataMin + 1;
        }

        var min = axis.MinLimit ?? dataMin;
        var max = axis.MaxLimit ?? dataMax;

        return (min, max);
    }
}
