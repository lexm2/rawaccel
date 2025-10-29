using System;
using userinterface.Charting.Core;
using userinterface.Charting.Series;

namespace userinterface.Charting.Extensions;

public class LoggingScatterSeries<T> : ScatterSeries<T>
{
    public event Action<ChartPoint>? PointClicked;

    public void LogPointClick(ChartPoint point)
    {
        PointClicked?.Invoke(point);
    }
}
