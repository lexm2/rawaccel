using System;
using userinterface.Charting.Core;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Series;

public class LineSeries<T> : SeriesBase, ILineSeries
{
    public IPaint? Stroke { get; set; }

    public IPaint? Fill { get; set; }

    public double LineSmoothness { get; set; }

    public double GeometrySize { get; set; }

    public IPaint? GeometryStroke { get; set; }

    public IPaint? GeometryFill { get; set; }

    public Func<ChartPoint, string>? XToolTipLabelFormatter { get; set; }

    public Func<ChartPoint, string>? YToolTipLabelFormatter { get; set; }
}
