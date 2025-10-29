using System;
using SkiaSharp;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Rendering;

public static class AxisRenderer
{
    private const float AxisMargin = 60f;
    private const float AxisTitleMargin = 40f;

    public static void RenderHorizontalAxis(SKCanvas canvas, IAxis axis, SKRect chartArea, double min, double max)
    {
        if (max <= min)
            return;

        DrawHorizontalSeparators(canvas, axis, chartArea, min, max);

        DrawHorizontalLabels(canvas, axis, chartArea, min, max);

        if (!string.IsNullOrEmpty(axis.Name) && axis.NamePaint != null)
        {
            DrawHorizontalTitle(canvas, axis, chartArea);
        }
    }

    public static void RenderVerticalAxis(SKCanvas canvas, IAxis axis, SKRect chartArea, double min, double max)
    {
        if (max <= min)
            return;

        DrawVerticalSeparators(canvas, axis, chartArea, min, max);

        DrawVerticalLabels(canvas, axis, chartArea, min, max);

        if (!string.IsNullOrEmpty(axis.Name) && axis.NamePaint != null)
        {
            DrawVerticalTitle(canvas, axis, chartArea);
        }
    }

    private static void DrawHorizontalSeparators(SKCanvas canvas, IAxis axis, SKRect chartArea, double min, double max)
    {
        if (axis.SeparatorsPaint == null)
            return;

        using var paint = new SKPaint
        {
            Color = axis.SeparatorsPaint.Color,
            StrokeWidth = axis.SeparatorsPaint.StrokeThickness,
            IsAntialias = true,
            Style = SKPaintStyle.Stroke
        };

        var interval = CalculateNiceInterval(max - min);
        var start = Math.Ceiling(min / interval) * interval;

        for (double value = start; value <= max; value += interval)
        {
            var x = chartArea.Left + (float)((value - min) / (max - min) * chartArea.Width);
            canvas.DrawLine(x, chartArea.Top, x, chartArea.Bottom, paint);
        }
    }

    private static void DrawVerticalSeparators(SKCanvas canvas, IAxis axis, SKRect chartArea, double min, double max)
    {
        if (axis.SeparatorsPaint == null)
            return;

        using var paint = new SKPaint
        {
            Color = axis.SeparatorsPaint.Color,
            StrokeWidth = axis.SeparatorsPaint.StrokeThickness,
            IsAntialias = true,
            Style = SKPaintStyle.Stroke
        };

        var interval = CalculateNiceInterval(max - min);
        var start = Math.Ceiling(min / interval) * interval;

        for (double value = start; value <= max; value += interval)
        {
            var y = chartArea.Bottom - (float)((value - min) / (max - min) * chartArea.Height);
            canvas.DrawLine(chartArea.Left, y, chartArea.Right, y, paint);
        }
    }

    private static void DrawHorizontalLabels(SKCanvas canvas, IAxis axis, SKRect chartArea, double min, double max)
    {
        if (axis.LabelsPaint == null)
            return;

        using var paint = new SKPaint
        {
            Color = axis.LabelsPaint.Color,
            TextSize = (float)axis.TextSize,
            IsAntialias = true,
            TextAlign = SKTextAlign.Center
        };

        var interval = CalculateNiceInterval(max - min);
        var start = Math.Ceiling(min / interval) * interval;

        for (double value = start; value <= max; value += interval)
        {
            var x = chartArea.Left + (float)((value - min) / (max - min) * chartArea.Width);
            var label = FormatAxisValue(value);
            canvas.DrawText(label, x, chartArea.Bottom + 20, paint);
        }
    }

    private static void DrawVerticalLabels(SKCanvas canvas, IAxis axis, SKRect chartArea, double min, double max)
    {
        if (axis.LabelsPaint == null)
            return;

        using var paint = new SKPaint
        {
            Color = axis.LabelsPaint.Color,
            TextSize = (float)axis.TextSize,
            IsAntialias = true,
            TextAlign = SKTextAlign.Right
        };

        var interval = CalculateNiceInterval(max - min);
        var start = Math.Ceiling(min / interval) * interval;

        for (double value = start; value <= max; value += interval)
        {
            var y = chartArea.Bottom - (float)((value - min) / (max - min) * chartArea.Height);
            var label = FormatAxisValue(value);
            canvas.DrawText(label, chartArea.Left - 10, y + ((float)axis.TextSize / 3), paint);
        }
    }

    private static void DrawHorizontalTitle(SKCanvas canvas, IAxis axis, SKRect chartArea)
    {
        using var paint = new SKPaint
        {
            Color = axis.NamePaint!.Color,
            TextSize = (float)axis.NameTextSize,
            IsAntialias = true,
            TextAlign = SKTextAlign.Center
        };

        var x = chartArea.Left + chartArea.Width / 2;
        var y = chartArea.Bottom + AxisTitleMargin;
        canvas.DrawText(axis.Name!, x, y, paint);
    }

    private static void DrawVerticalTitle(SKCanvas canvas, IAxis axis, SKRect chartArea)
    {
        using var paint = new SKPaint
        {
            Color = axis.NamePaint!.Color,
            TextSize = (float)axis.NameTextSize,
            IsAntialias = true,
            TextAlign = SKTextAlign.Center
        };

        canvas.Save();

        var x = 15f;
        var y = chartArea.Top + chartArea.Height / 2;

        canvas.Translate(x, y);
        canvas.RotateDegrees(-90);
        canvas.DrawText(axis.Name!, 0, 0, paint);

        canvas.Restore();
    }

    private static double CalculateNiceInterval(double range)
    {
        if (range <= 0)
            return 1;

        var magnitude = Math.Pow(10, Math.Floor(Math.Log10(range)));
        var fraction = range / magnitude;

        double niceFraction;
        if (fraction < 1.5)
            niceFraction = 1;
        else if (fraction < 3)
            niceFraction = 2;
        else if (fraction < 7)
            niceFraction = 5;
        else
            niceFraction = 10;

        return niceFraction * magnitude;
    }

    private static string FormatAxisValue(double value)
    {
        if (Math.Abs(value) < 0.001)
            return "0";

        if (Math.Abs(value) >= 1000)
            return value.ToString("N0");

        if (Math.Abs(value) >= 10)
            return value.ToString("F1");

        return value.ToString("F2");
    }
}
