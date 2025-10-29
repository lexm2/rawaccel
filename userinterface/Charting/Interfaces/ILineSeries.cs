using System;
using userinterface.Charting.Core;

namespace userinterface.Charting.Interfaces;

public interface ILineSeries : ISeries
{
    IPaint? Stroke { get; set; }

    IPaint? Fill { get; set; }

    double LineSmoothness { get; set; }

    double GeometrySize { get; set; }

    IPaint? GeometryStroke { get; set; }

    IPaint? GeometryFill { get; set; }

    Func<ChartPoint, string>? XToolTipLabelFormatter { get; set; }

    Func<ChartPoint, string>? YToolTipLabelFormatter { get; set; }
}
