using System.Collections.Generic;
using System.Linq;
using SkiaSharp;
using userinterface.Charting.Core;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Rendering;

public static class SeriesRenderer
{
    public static void RenderLineSeries(SKCanvas canvas, ILineSeries series, SKRect chartArea, double xMin, double xMax, double yMin, double yMax)
    {
        if (series.Values == null || series.Mapping == null)
            return;

        var points = new List<SKPoint>();
        int index = 0;

        foreach (var item in (System.Collections.IEnumerable)series.Values)
        {
            var chartPoint = series.Mapping(item, index++);
            var pixelPoint = ChartMath.DataToPixels(chartPoint, chartArea, xMin, xMax, yMin, yMax);
            points.Add(pixelPoint);
        }

        if (points.Count == 0)
            return;

        DrawLinePath(canvas, points, series.Stroke, series.LineSmoothness);

        if (series.GeometrySize > 0)
        {
            DrawGeometry(canvas, points, series.GeometrySize, series.GeometryFill, series.GeometryStroke);
        }
    }

    public static void RenderScatterSeries(SKCanvas canvas, IScatterSeries series, SKRect chartArea, double xMin, double xMax, double yMin, double yMax)
    {
        if (series.Values == null || series.Mapping == null)
            return;

        var points = new List<SKPoint>();
        int index = 0;

        foreach (var item in (System.Collections.IEnumerable)series.Values)
        {
            var chartPoint = series.Mapping(item, index++);
            var pixelPoint = ChartMath.DataToPixels(chartPoint, chartArea, xMin, xMax, yMin, yMax);
            points.Add(pixelPoint);
        }

        if (points.Count == 0)
            return;

        DrawGeometry(canvas, points, series.GeometrySize, series.Fill, series.Stroke);
    }

    private static void DrawLinePath(SKCanvas canvas, List<SKPoint> points, IPaint? stroke, double smoothness)
    {
        if (points.Count < 2 || stroke == null)
            return;

        using var paint = new SKPaint
        {
            Color = stroke.Color,
            StrokeWidth = stroke.StrokeThickness,
            IsAntialias = true,
            Style = SKPaintStyle.Stroke
        };

        using var path = new SKPath();
        path.MoveTo(points[0]);

        for (int i = 1; i < points.Count; i++)
        {
            path.LineTo(points[i]);
        }

        canvas.DrawPath(path, paint);
    }

    private static void DrawGeometry(SKCanvas canvas, List<SKPoint> points, double size, IPaint? fill, IPaint? stroke)
    {
        if (size <= 0)
            return;

        float radius = (float)size / 2;

        using var fillPaint = fill != null ? new SKPaint
        {
            Color = fill.Color,
            IsAntialias = true,
            Style = SKPaintStyle.Fill
        } : null;

        using var strokePaint = stroke != null ? new SKPaint
        {
            Color = stroke.Color,
            StrokeWidth = stroke.StrokeThickness,
            IsAntialias = true,
            Style = SKPaintStyle.Stroke
        } : null;

        foreach (var point in points)
        {
            if (fillPaint != null)
                canvas.DrawCircle(point, radius, fillPaint);

            if (strokePaint != null)
                canvas.DrawCircle(point, radius, strokePaint);
        }
    }
}
