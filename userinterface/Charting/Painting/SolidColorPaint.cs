using SkiaSharp;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Painting;

public class SolidColorPaint : IPaint
{
    public SKColor Color { get; set; }

    public float StrokeThickness { get; set; }

    public SolidColorPaint(SKColor color, float strokeThickness = 1f)
    {
        Color = color;
        StrokeThickness = strokeThickness;
    }
}
