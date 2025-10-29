using System;

namespace userinterface.Charting.Interfaces;

public interface IAxis
{
    string? Name { get; set; }

    double NameTextSize { get; set; }

    IPaint? NamePaint { get; set; }

    double TextSize { get; set; }

    IPaint? LabelsPaint { get; set; }

    IPaint? SeparatorsPaint { get; set; }

    IPaint? SubseparatorsPaint { get; set; }

    IPaint? TicksPaint { get; set; }

    double? MinLimit { get; set; }

    double? MaxLimit { get; set; }

    TimeSpan AnimationsSpeed { get; set; }

    Func<double, double>? EasingFunction { get; set; }
}
