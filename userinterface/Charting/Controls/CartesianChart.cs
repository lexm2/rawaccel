using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using SkiaSharp;
using userinterface.Charting.Core;
using userinterface.Charting.Interfaces;
using userinterface.Charting.Rendering;

namespace userinterface.Charting.Controls;

public class CartesianChart : Control, ICartesianChart
{
    public static readonly StyledProperty<IEnumerable<ISeries>?> SeriesProperty =
        AvaloniaProperty.Register<CartesianChart, IEnumerable<ISeries>?>(nameof(Series));

    public static readonly StyledProperty<IEnumerable<IAxis>?> XAxesProperty =
        AvaloniaProperty.Register<CartesianChart, IEnumerable<IAxis>?>(nameof(XAxes));

    public static readonly StyledProperty<IEnumerable<IAxis>?> YAxesProperty =
        AvaloniaProperty.Register<CartesianChart, IEnumerable<IAxis>?>(nameof(YAxes));

    public static readonly StyledProperty<IPaint?> TooltipTextPaintProperty =
        AvaloniaProperty.Register<CartesianChart, IPaint?>(nameof(TooltipTextPaint));

    public static readonly StyledProperty<IPaint?> TooltipBackgroundPaintProperty =
        AvaloniaProperty.Register<CartesianChart, IPaint?>(nameof(TooltipBackgroundPaint));

    public static readonly StyledProperty<double> TooltipTextSizeProperty =
        AvaloniaProperty.Register<CartesianChart, double>(nameof(TooltipTextSize), 12.0);

    public static readonly StyledProperty<bool> AutoUpdateEnabledProperty =
        AvaloniaProperty.Register<CartesianChart, bool>(nameof(AutoUpdateEnabled), true);

    public static readonly StyledProperty<IBrush?> BackgroundProperty =
        AvaloniaProperty.Register<CartesianChart, IBrush?>(nameof(Background));

    static CartesianChart()
    {
        AffectsRender<CartesianChart>(
            SeriesProperty,
            XAxesProperty,
            YAxesProperty,
            TooltipTextPaintProperty,
            TooltipBackgroundPaintProperty,
            TooltipTextSizeProperty,
            AutoUpdateEnabledProperty,
            BackgroundProperty);
    }

    private readonly ChartRenderer renderer = new();

    public IEnumerable<ISeries>? Series
    {
        get => GetValue(SeriesProperty);
        set => SetValue(SeriesProperty, value);
    }

    public IEnumerable<IAxis>? XAxes
    {
        get => GetValue(XAxesProperty);
        set => SetValue(XAxesProperty, value);
    }

    public IEnumerable<IAxis>? YAxes
    {
        get => GetValue(YAxesProperty);
        set => SetValue(YAxesProperty, value);
    }

    public IPaint? TooltipTextPaint
    {
        get => GetValue(TooltipTextPaintProperty);
        set => SetValue(TooltipTextPaintProperty, value);
    }

    public IPaint? TooltipBackgroundPaint
    {
        get => GetValue(TooltipBackgroundPaintProperty);
        set => SetValue(TooltipBackgroundPaintProperty, value);
    }

    public double TooltipTextSize
    {
        get => GetValue(TooltipTextSizeProperty);
        set => SetValue(TooltipTextSizeProperty, value);
    }

    public bool AutoUpdateEnabled
    {
        get => GetValue(AutoUpdateEnabledProperty);
        set => SetValue(AutoUpdateEnabledProperty, value);
    }

    public IBrush? Background
    {
        get => GetValue(BackgroundProperty);
        set => SetValue(BackgroundProperty, value);
    }

    public SKPoint ScaleDataToPixels(ChartPoint dataPoint)
    {
        var chartArea = renderer.GetChartArea();
        var (xMin, xMax, yMin, yMax) = renderer.GetRanges();
        return ChartMath.DataToPixels(dataPoint, chartArea, xMin, xMax, yMin, yMax);
    }

    public ChartPoint ScalePixelsToData(SKPoint pixelPoint)
    {
        var chartArea = renderer.GetChartArea();
        var (xMin, xMax, yMin, yMax) = renderer.GetRanges();
        return ChartMath.PixelsToData(pixelPoint, chartArea, xMin, xMax, yMin, yMax);
    }

    public sealed override void Render(DrawingContext context)
    {
        base.Render(context);

        var bounds = Bounds;
        if (bounds.Width <= 0 || bounds.Height <= 0)
            return;

        if (Background != null)
        {
            context.FillRectangle(Background, new Rect(bounds.Size));
        }

        context.Custom(new ChartCustomDrawOperation(bounds, renderer, Series, XAxes, YAxes));
    }

    private class ChartCustomDrawOperation : ICustomDrawOperation
    {
        private readonly Rect bounds;
        private readonly ChartRenderer renderer;
        private readonly IEnumerable<ISeries>? series;
        private readonly IEnumerable<IAxis>? xAxes;
        private readonly IEnumerable<IAxis>? yAxes;

        public ChartCustomDrawOperation(Rect bounds, ChartRenderer renderer, IEnumerable<ISeries>? series, IEnumerable<IAxis>? xAxes, IEnumerable<IAxis>? yAxes)
        {
            this.bounds = bounds;
            this.renderer = renderer;
            this.series = series;
            this.xAxes = xAxes;
            this.yAxes = yAxes;
        }

        public Rect Bounds => bounds;

        public bool HitTest(Point p) => false;

        public bool Equals(ICustomDrawOperation? other) => false;

        public void Dispose()
        {
        }

        public void Render(ImmediateDrawingContext context)
        {
            var leaseFeature = context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature)) as ISkiaSharpApiLeaseFeature;
            if (leaseFeature == null)
                return;

            using var lease = leaseFeature.Lease();
            var canvas = lease.SkCanvas;

            renderer.Render(canvas, series, xAxes, yAxes, new SKRect(0, 0, (float)bounds.Width, (float)bounds.Height));
        }
    }
}
