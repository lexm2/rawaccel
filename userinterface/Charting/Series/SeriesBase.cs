using System;
using System.Collections.Generic;
using userinterface.Charting.Core;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Series;

public abstract class SeriesBase : ISeries
{
    public string? Name { get; set; }

    public bool IsVisible { get; set; } = true;

    public object? Values { get; set; }

    public Func<object, int, ChartPoint>? Mapping { get; set; }

    public TimeSpan AnimationsSpeed { get; set; } = TimeSpan.FromMilliseconds(100);

    protected IEnumerable<ChartPoint> GetMappedPoints()
    {
        if (Values == null || Mapping == null)
            yield break;

        int index = 0;
        foreach (var item in (System.Collections.IEnumerable)Values)
        {
            yield return Mapping(item, index++);
        }
    }
}
