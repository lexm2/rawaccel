namespace userinterface.Charting.Core;

public readonly struct ChartPoint
{
    public double X { get; init; }

    public double Y { get; init; }

    public ChartPoint(double x, double y)
    {
        X = x;
        Y = y;
    }
}
