using System;
using System.Collections.ObjectModel;
using SkiaSharp;
using userinterface.Charting.Core;
using userinterface.Charting.Extensions;
using userinterface.Charting.Painting;
using userinterface.Charting.Series;
using userspace_backend.Display;

namespace userinterface.Services.Charting;

public interface IChartSeriesManager
{
    LineSeries<CurvePoint> CreateLineSeries(
        ObservableCollection<CurvePoint> points,
        SolidColorPaint stroke,
        string name,
        string axisLabel);

    ScatterSeries<CurvePoint> CreateScatterSeries(
        ObservableCollection<CurvePoint> data,
        string name,
        SKColor strokeColor,
        SKColor fillColor,
        double geometrySize = 8,
        float strokeThickness = 2);

    LoggingScatterSeries<CurvePoint> CreateLUTSeries(
        ObservableCollection<CurvePoint>? data,
        string name,
        SKColor strokeColor,
        SKColor fillColor,
        Action<ChartPoint>? onPointClicked = null,
        double geometrySize = 8,
        float strokeThickness = 2);

    SolidColorPaint GetDefaultXStroke(float strokeThickness = 2);

    SolidColorPaint GetDefaultYStroke(float strokeThickness = 2);
}
