namespace userinterface.Charting.Core;

public readonly struct ChartPadding
{
    public double X { get; init; }

    public double Y { get; init; }

    public ChartPadding(double x, double y)
    {
        X = x;
        Y = y;
    }
}
