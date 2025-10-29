using System;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Axes;

public class Axis : IAxis
{
    public string? Name { get; set; }

    public double NameTextSize { get; set; } = 14;

    public IPaint? NamePaint { get; set; }

    public double TextSize { get; set; } = 12;

    public IPaint? LabelsPaint { get; set; }

    public IPaint? SeparatorsPaint { get; set; }

    public IPaint? SubseparatorsPaint { get; set; }

    public IPaint? TicksPaint { get; set; }

    public double? MinLimit { get; set; }

    public double? MaxLimit { get; set; }

    public TimeSpan AnimationsSpeed { get; set; } = TimeSpan.FromMilliseconds(100);

    public Func<double, double>? EasingFunction { get; set; }
}
