using System;
using System.Collections.Generic;
using System.Linq;
using SkiaSharp;
using userinterface.Charting.Axes;
using userinterface.Charting.Painting;
using userinterface.Services;
using userspace_backend.Display;

namespace userinterface.Services.Charting;

public class ChartAxisManager : IChartAxisManager
{
    private const int AxisNameTextSize = 14;
    private const int AxisTextSize = 12;
    private const float StandardStrokeThickness = 1;
    private const float SubStrokeThickness = 0.5f;
    private const byte SubSeparatorAlpha = 50;
    private const int DefaultMaxX = 500;
    private const int DefaultMaxY = 2;
    private const double DataPaddingRatio = 0.1;
    private const int DefaultAxisRange = 50;
    private const int DefaultYRange = 1;

    private static readonly string AxisTitleBrush = "PrimaryTextBrush";
    private static readonly string AxisLabelsBrush = "SecondaryTextBrush";
    private static readonly string AxisSeparatorsBrush = "BorderBrush";

    private readonly IThemeService themeService;

    public ChartAxisManager(IThemeService themeService)
    {
        this.themeService = themeService;
    }

    public Axis[] CreateXAxes(string name, double? minLimit = null, double? maxLimit = null)
    {
        return CreateAxis(name, minLimit, maxLimit);
    }

    public Axis[] CreateYAxes(string name, double? minLimit = null, double? maxLimit = null)
    {
        return CreateAxis(name, minLimit, maxLimit);
    }

    public (double minX, double maxX, double minY, double maxY) CalculateDataBounds(IEnumerable<CurvePoint> points)
    {
        var pointsList = points.ToList();
        if (pointsList.Count == 0)
        {
            return (0, DefaultMaxX, 0, DefaultMaxY);
        }

        var minX = pointsList.Min(p => p.MouseSpeed);
        var maxX = pointsList.Max(p => p.MouseSpeed);
        var minY = pointsList.Min(p => p.Output);
        var maxY = pointsList.Max(p => p.Output);

        return (minX, maxX, minY, maxY);
    }

    public (double xMin, double xMax, double yMin, double yMax) CalculateFitToDataLimits(
        IEnumerable<CurvePoint> points,
        double currentMaxXAxisLimit,
        double currentMaxYAxisLimit)
    {
        var pointsList = points.ToList();
        if (pointsList.Count == 0)
        {
            return (0, Math.Max(currentMaxXAxisLimit, DefaultMaxX), 0, Math.Max(currentMaxYAxisLimit, DefaultMaxY));
        }

        var (minX, maxX, minY, maxY) = CalculateDataBounds(pointsList);

        if (maxY == minY)
        {
            return CalculateCenteredLimits(minX, maxX, minY, maxY, currentMaxXAxisLimit, currentMaxYAxisLimit);
        }
        else
        {
            return CalculatePaddedLimits(minX, maxX, minY, maxY, currentMaxXAxisLimit, currentMaxYAxisLimit);
        }
    }

    public void UpdateAxisLimits(
        Axis[] xAxes,
        Axis[] yAxes,
        double xMin,
        double xMax,
        double yMin,
        double yMax)
    {
        if (xAxes.Length > 0)
        {
            xAxes[0].MinLimit = xMin;
            xAxes[0].MaxLimit = xMax;
        }

        if (yAxes.Length > 0)
        {
            yAxes[0].MinLimit = yMin;
            yAxes[0].MaxLimit = yMax;
        }
    }

    private Axis[] CreateAxis(string name, double? minLimit, double? maxLimit)
    {
        var titleColor = themeService.GetCachedColor(AxisTitleBrush);
        var labelColor = themeService.GetCachedColor(AxisLabelsBrush);
        var separatorColor = themeService.GetCachedColor(AxisSeparatorsBrush);

        return new Axis[]
        {
            new Axis()
            {
                Name = name,
                NameTextSize = AxisNameTextSize,
                NamePaint = new SolidColorPaint(titleColor),
                LabelsPaint = new SolidColorPaint(labelColor),
                TextSize = AxisTextSize,
                SeparatorsPaint = new SolidColorPaint(separatorColor) { StrokeThickness = StandardStrokeThickness },
                TicksPaint = new SolidColorPaint(titleColor) { StrokeThickness = StandardStrokeThickness },
                SubseparatorsPaint = new SolidColorPaint(separatorColor.WithAlpha(SubSeparatorAlpha)) { StrokeThickness = SubStrokeThickness },
                AnimationsSpeed = TimeSpan.FromMilliseconds(100),
                MinLimit = minLimit,
                MaxLimit = maxLimit
            }
        };
    }

    private (double xMin, double xMax, double yMin, double yMax) CalculateCenteredLimits(
        double minX,
        double maxX,
        double minY,
        double maxY,
        double currentMaxXAxisLimit,
        double currentMaxYAxisLimit)
    {
        var centerY = (minY + maxY) / 2;
        var centerX = (minX + maxX) / 2;

        var yMin = Math.Max(0, centerY - DefaultYRange);
        var yMax = Math.Max(currentMaxYAxisLimit, centerY + DefaultYRange);
        var xMin = Math.Max(0, centerX - DefaultAxisRange);
        var xMax = Math.Max(currentMaxXAxisLimit, centerX + DefaultAxisRange);

        return (xMin, xMax, yMin, yMax);
    }

    private (double xMin, double xMax, double yMin, double yMax) CalculatePaddedLimits(
        double minX,
        double maxX,
        double minY,
        double maxY,
        double currentMaxXAxisLimit,
        double currentMaxYAxisLimit)
    {
        var xRange = maxX - minX;
        var yRange = maxY - minY;
        var xPadding = xRange * DataPaddingRatio;
        var yPadding = yRange * DataPaddingRatio;

        var xMin = Math.Max(0, minX - xPadding);
        var xMax = Math.Max(currentMaxXAxisLimit, maxX + xPadding);
        var yMin = Math.Max(0, minY - yPadding);
        var yMax = Math.Max(currentMaxYAxisLimit, maxY + yPadding);

        return (xMin, xMax, yMin, yMax);
    }
}
