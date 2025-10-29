using System.Collections.Generic;
using userinterface.Charting.Axes;
using userspace_backend.Display;

namespace userinterface.Services.Charting;

public interface IChartAxisManager
{
    Axis[] CreateXAxes(string name, double? minLimit = null, double? maxLimit = null);

    Axis[] CreateYAxes(string name, double? minLimit = null, double? maxLimit = null);

    (double minX, double maxX, double minY, double maxY) CalculateDataBounds(IEnumerable<CurvePoint> points);

    (double xMin, double xMax, double yMin, double yMax) CalculateFitToDataLimits(
        IEnumerable<CurvePoint> points,
        double currentMaxXAxisLimit,
        double currentMaxYAxisLimit);

    void UpdateAxisLimits(
        Axis[] xAxes,
        Axis[] yAxes,
        double xMin,
        double xMax,
        double yMin,
        double yMax);
}
