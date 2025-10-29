using System;
using userinterface.Charting.Core;

namespace userinterface.Charting.Interfaces;

public interface ISeries
{
    string? Name { get; set; }

    bool IsVisible { get; set; }

    object? Values { get; set; }

    Func<object, int, ChartPoint>? Mapping { get; set; }

    TimeSpan AnimationsSpeed { get; set; }
}
