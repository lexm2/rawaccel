using System.Collections.Generic;
using SkiaSharp;
using userinterface.Charting.Interfaces;
using userinterface.Charting.Rendering;

namespace userinterface.Charting.Controls;

public class HeadlessCartesianChart
{
    public int Width { get; set; }

    public int Height { get; set; }

    public IEnumerable<ISeries>? Series { get; set; }

    public IEnumerable<IAxis>? XAxes { get; set; }

    public IEnumerable<IAxis>? YAxes { get; set; }

    public SKColor Background { get; set; } = SKColors.White;

    private readonly ChartRenderer renderer = new();

    public SKImage GetImage()
    {
        var info = new SKImageInfo(Width, Height);
        var surface = SKSurface.Create(info);
        var canvas = surface.Canvas;

        canvas.Clear(Background);

        renderer.Render(canvas, Series, XAxes, YAxes, new SKRect(0, 0, Width, Height));

        return surface.Snapshot();
    }
}
