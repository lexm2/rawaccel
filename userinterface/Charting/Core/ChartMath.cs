using SkiaSharp;

namespace userinterface.Charting.Core;

public static class ChartMath
{
    public static SKPoint DataToPixels(ChartPoint data, SKRect chartArea, double xMin, double xMax, double yMin, double yMax)
    {
        if (xMax <= xMin || yMax <= yMin)
            return new SKPoint(chartArea.Left, chartArea.Bottom);

        var xPixel = chartArea.Left + (float)((data.X - xMin) / (xMax - xMin) * chartArea.Width);
        var yPixel = chartArea.Bottom - (float)((data.Y - yMin) / (yMax - yMin) * chartArea.Height);

        return new SKPoint(xPixel, yPixel);
    }

    public static ChartPoint PixelsToData(SKPoint pixels, SKRect chartArea, double xMin, double xMax, double yMin, double yMax)
    {
        if (xMax <= xMin || yMax <= yMin)
            return new ChartPoint(xMin, yMin);

        var x = xMin + (pixels.X - chartArea.Left) / chartArea.Width * (xMax - xMin);
        var y = yMin + (chartArea.Bottom - pixels.Y) / chartArea.Height * (yMax - yMin);

        return new ChartPoint(x, y);
    }
}
