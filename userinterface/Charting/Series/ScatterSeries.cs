using userinterface.Charting.Core;
using userinterface.Charting.Interfaces;

namespace userinterface.Charting.Series;

public class ScatterSeries<T> : SeriesBase, IScatterSeries
{
    public double GeometrySize { get; set; } = 8;

    public IPaint? Stroke { get; set; }

    public IPaint? Fill { get; set; }

    public ChartPadding DataPadding { get; set; }
}
