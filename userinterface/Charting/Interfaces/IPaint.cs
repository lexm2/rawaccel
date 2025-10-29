using SkiaSharp;

namespace userinterface.Charting.Interfaces;

public interface IPaint
{
    SKColor Color { get; set; }

    float StrokeThickness { get; set; }
}
