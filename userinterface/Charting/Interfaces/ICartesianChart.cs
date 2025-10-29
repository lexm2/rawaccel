using System.Collections.Generic;
using SkiaSharp;
using userinterface.Charting.Core;

namespace userinterface.Charting.Interfaces;

public interface ICartesianChart
{
    IEnumerable<ISeries>? Series { get; set; }

    IEnumerable<IAxis>? XAxes { get; set; }

    IEnumerable<IAxis>? YAxes { get; set; }

    IPaint? TooltipTextPaint { get; set; }

    IPaint? TooltipBackgroundPaint { get; set; }

    double TooltipTextSize { get; set; }

    bool AutoUpdateEnabled { get; set; }

    SKPoint ScaleDataToPixels(ChartPoint dataPoint);

    ChartPoint ScalePixelsToData(SKPoint pixelPoint);
}
