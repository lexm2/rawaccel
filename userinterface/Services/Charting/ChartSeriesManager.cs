using System;
using System.Collections.ObjectModel;
using SkiaSharp;
using userinterface.Charting.Core;
using userinterface.Charting.Extensions;
using userinterface.Charting.Painting;
using userinterface.Charting.Series;
using userspace_backend.Display;

namespace userinterface.Services.Charting;

public class ChartSeriesManager : IChartSeriesManager
{
    private const float DefaultStrokeThickness = 2;

    public LineSeries<CurvePoint> CreateLineSeries(
        ObservableCollection<CurvePoint> points,
        SolidColorPaint stroke,
        string name,
        string axisLabel)
    {
        return new LineSeries<CurvePoint>
        {
            Values = points,
            Fill = null,
            Stroke = stroke,
            Mapping = (object curvePointObj, int index) =>
            {
                var curvePoint = (CurvePoint)curvePointObj;
                return new ChartPoint(curvePoint.MouseSpeed, curvePoint.Output);
            },
            GeometrySize = 0,
            GeometryStroke = null,
            GeometryFill = null,
            AnimationsSpeed = TimeSpan.FromMilliseconds(100),
            Name = name,
            LineSmoothness = 0,
            XToolTipLabelFormatter = (chartPoint) => $"Speed: {chartPoint.X:F2}",
            YToolTipLabelFormatter = (chartPoint) => $"{axisLabel} Output: {chartPoint.Y:F2}"
        };
    }

    public ScatterSeries<CurvePoint> CreateScatterSeries(
        ObservableCollection<CurvePoint> data,
        string name,
        SKColor strokeColor,
        SKColor fillColor,
        double geometrySize = 8,
        float strokeThickness = 2)
    {
        return new ScatterSeries<CurvePoint>
        {
            Values = data,
            GeometrySize = geometrySize,
            Stroke = new SolidColorPaint(strokeColor) { StrokeThickness = strokeThickness },
            Fill = new SolidColorPaint(fillColor),
            Mapping = (object curvePointObj, int index) =>
            {
                var curvePoint = (CurvePoint)curvePointObj;
                return new ChartPoint(curvePoint.MouseSpeed, curvePoint.Output);
            },
            Name = name,
            IsVisible = false,
            DataPadding = new ChartPadding(0, 0)
        };
    }

    public LoggingScatterSeries<CurvePoint> CreateLUTSeries(
        ObservableCollection<CurvePoint>? data,
        string name,
        SKColor strokeColor,
        SKColor fillColor,
        Action<ChartPoint>? onPointClicked = null,
        double geometrySize = 8,
        float strokeThickness = 2)
    {
        var series = new LoggingScatterSeries<CurvePoint>
        {
            Values = data,
            GeometrySize = geometrySize,
            Stroke = new SolidColorPaint(strokeColor) { StrokeThickness = strokeThickness },
            Fill = new SolidColorPaint(fillColor),
            Mapping = (object curvePointObj, int index) =>
            {
                var curvePoint = (CurvePoint)curvePointObj;
                return new ChartPoint(curvePoint.MouseSpeed, curvePoint.Output);
            },
            Name = name,
            IsVisible = false,
            DataPadding = new ChartPadding(0, 0)
        };

        if (onPointClicked != null)
        {
            series.PointClicked += onPointClicked;
        }

        return series;
    }

    public SolidColorPaint GetDefaultXStroke(float strokeThickness = DefaultStrokeThickness)
    {
        return new SolidColorPaint(SKColors.CornflowerBlue) { StrokeThickness = strokeThickness };
    }

    public SolidColorPaint GetDefaultYStroke(float strokeThickness = DefaultStrokeThickness)
    {
        return new SolidColorPaint(SKColors.OrangeRed) { StrokeThickness = strokeThickness };
    }
}
