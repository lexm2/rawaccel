using userinterface.Charting.Core;

namespace userinterface.Charting.Interfaces;

public interface IScatterSeries : ISeries
{
    double GeometrySize { get; set; }

    IPaint? Stroke { get; set; }

    IPaint? Fill { get; set; }

    ChartPadding DataPadding { get; set; }
}
