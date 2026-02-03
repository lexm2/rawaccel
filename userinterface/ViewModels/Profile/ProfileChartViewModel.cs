using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Media.Immutable;
using LiveChartsCore;
using LiveChartsCore.SkiaSharpView;
using LiveChartsCore.SkiaSharpView.Painting;
using SkiaSharp;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Interfaces;
using userinterface.Services;
using userspace_backend.Display;
using userspace_backend.Model.EditableSettings;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Profile
{
    public partial class ProfileChartViewModel : ViewModelBase, IAsyncInitializable
    {
        // Animation settings
        private const int AnimationMilliseconds = 200;

        // Data fitting and bounds
        private const double DataPaddingRatio = 0.1;


        // Default chart limits when no data or centering
        private const int DefaultAxisRange = 50;

        private const int DefaultYRange = 1;
        private const int DefaultMaxX = 100;
        private const int DefaultMaxY = 2;

        // Line and stroke thickness
        private const int MainStrokeThickness = 2;

        private const int StandardStrokeThickness = 1;
        private const float SubStrokeThickness = 0.5f;

        // Color transparency values
        private const byte SubSeparatorAlpha = 100;

        private const byte TooltipBackgroundAlpha = 180;

        // Theme color resource keys
        private static readonly string AxisTitleBrush = "PrimaryTextBrush";
        private static readonly string AxisLabelsBrush = "SecondaryTextBrush";
        private static readonly string AxisSeparatorsBrush = "BorderBrush";
        private static readonly string TooltipBackgroundBrush = "CardBackgroundBrush";

        // Axis labeling and text - will be set by localization service
        private const int AxisNameTextSize = 14;
        private const int AxisTextSize = 12;

        public static readonly TimeSpan AnimationsTime = new(days: 0, hours: 0, minutes: 0, seconds: 0, milliseconds: AnimationMilliseconds);

        private readonly IThemeService themeService;
        private readonly LocalizationService localizationService;
        private readonly PreviewChartRenderer previewRenderer;
        private readonly MouseInputMonitorService mouseInputMonitor;
        private BE.IProfileModel currentProfileModel = null!;

        // Sync object for thread safety - single allocation
        private readonly object syncObject = new object();

        // Cached series instances for animation continuity
        private LineSeries<CurvePoint>? cachedXSeries;
        private LineSeries<CurvePoint>? cachedYSeries;

        // Chart control reference for coordinate transformation
        private LiveChartsCore.SkiaSharpView.Avalonia.CartesianChart? chartControl;

        // Last mouse speed for refreshing dot positions on zoom/resize
        private double? lastMouseSpeed = null;

        public ProfileChartViewModel(IThemeService themeService, LocalizationService localizationService, PreviewChartRenderer previewRenderer, MouseInputMonitorService mouseInputMonitor)
        {
            this.themeService = themeService ?? throw new ArgumentNullException(nameof(themeService));
            this.localizationService = localizationService ?? throw new ArgumentNullException(nameof(localizationService));
            this.previewRenderer = previewRenderer ?? throw new ArgumentNullException(nameof(previewRenderer));
            this.mouseInputMonitor = mouseInputMonitor ?? throw new ArgumentNullException(nameof(mouseInputMonitor));

            // Subscribe to mouse speed updates
            this.mouseInputMonitor.MouseSpeedUpdated += OnMouseSpeedUpdated;

            RecreateAxesCommand = new RelayCommand(() => 
            {
                EnsureInteractiveChartLoaded();
                RecreateAxes();
            });
            FitToDataCommand = new RelayCommand(() =>
            {
                EnsureInteractiveChartLoaded();
                FitToData();
            });
        }

        public bool IsInitialized { get; private set; }

        public bool IsInitializing { get; private set; }

        public bool IsInteractiveMode { get; private set; } = false;

        public bool IsDebugDriverActive => App.IsDebugDriver;

        public bool IsLoadingChart { get; private set; } = false;

        public double ChartOpacity { get; private set; } = 0.0;

        private bool hasUserInteracted = false;

        // Input indicator visibility
        private bool showInputIndicator = false;
        public bool ShowInputIndicator
        {
            get => showInputIndicator;
            set
            {
                if (showInputIndicator != value)
                {
                    showInputIndicator = value;
                    OnPropertyChanged(nameof(ShowInputIndicator));

                    // Start/stop monitoring when toggled
                    if (value)
                    {
                        mouseInputMonitor.StartMonitoring();
                    }
                    else
                    {
                        mouseInputMonitor.StopMonitoring();
                    }
                }
            }
        }

        // Show Y curve dot when YX ratio != 1.0
        public bool ShowYCurveDot => ShowInputIndicator && YXRatio?.CurrentValidatedValue != 1.0;

        // X Curve Dot Position (in pixels)
        private double xDotPixelX = 0;
        public double XDotPixelX
        {
            get => xDotPixelX;
            private set
            {
                if (Math.Abs(xDotPixelX - value) > 0.1)
                {
                    xDotPixelX = value;
                    OnPropertyChanged(nameof(XDotPixelX));
                }
            }
        }

        private double xDotPixelY = 0;
        public double XDotPixelY
        {
            get => xDotPixelY;
            private set
            {
                if (Math.Abs(xDotPixelY - value) > 0.1)
                {
                    xDotPixelY = value;
                    OnPropertyChanged(nameof(XDotPixelY));
                }
            }
        }

        // Y Curve Dot Position (in pixels)
        private double yDotPixelX = 0;
        public double YDotPixelX
        {
            get => yDotPixelX;
            private set
            {
                if (Math.Abs(yDotPixelX - value) > 0.1)
                {
                    yDotPixelX = value;
                    OnPropertyChanged(nameof(YDotPixelX));
                }
            }
        }

        private double yDotPixelY = 0;
        public double YDotPixelY
        {
            get => yDotPixelY;
            private set
            {
                if (Math.Abs(yDotPixelY - value) > 0.1)
                {
                    yDotPixelY = value;
                    OnPropertyChanged(nameof(YDotPixelY));
                }
            }
        }

        public object Sync => syncObject;

        private ICurvePreview XCurvePreview { get; set; } = null!;

        private ICurvePreview YCurvePreview { get; set; } = null!;

        private IEditableSettingSpecific<double> YXRatio { get; set; } = null!;

        public void Initialize(BE.IProfileModel profileModel)
        {
            if (currentProfileModel == profileModel)
                return;

            currentProfileModel = profileModel;
            XCurvePreview = profileModel.XCurvePreview;
            YCurvePreview = profileModel.YCurvePreview;
            YXRatio = profileModel.YXRatio;

            YXRatio.PropertyChanged += OnYXRatioChanged;

            // Subscribe to curve preview changes
            XCurvePreview.Points.CollectionChanged += OnCurvePointsChanged;
            YCurvePreview.Points.CollectionChanged += OnCurvePointsChanged;
        }


        // ================================================================================================
        // INITIALIZATION & SETUP
        // ================================================================================================

        public Task InitializeAsync()
        {
            if (IsInitializing || IsInitialized || currentProfileModel == null)
                return Task.CompletedTask;

            IsInitializing = true;

            try
            {
                // Show skeleton loader immediately
                IsLoadingChart = true;
                OnPropertyChanged(nameof(IsLoadingChart));
                
                // Initialize interactive chart
                _ = Task.Run(async () =>
                {
                    try
                    {
                        // Small delay to show skeleton loader
                        await Task.Delay(100);

                        // All UI updates must happen on UI thread (including Series modification)
                        Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                        {
                            try
                            {
                                Series.Clear();
                                CreateSeries();

                                // Calculate proper axis limits based on data BEFORE creating axes
                                var (xMin, xMax, yMin, yMax) = CalculateInitialAxisLimits();

                                // Create axes with correct limits from the start
                                XAxes = CreateXAxes(xMin, xMax);
                                YAxes = CreateYAxes(yMin, yMax);
                                TooltipTextPaint = new SolidColorPaint(themeService.GetCachedColor(AxisTitleBrush));
                                TooltipBackgroundPaint = new SolidColorPaint(themeService.GetCachedColor(TooltipBackgroundBrush).WithAlpha(TooltipBackgroundAlpha));

                                // Subscribe to events
                                this.themeService.ThemeChanged += OnThemeChanged;
                                this.localizationService.PropertyChanged += OnLocalizationChanged;

                                // Notify UI of changes
                                OnPropertyChanged(nameof(XAxes));
                                OnPropertyChanged(nameof(YAxes));
                                OnPropertyChanged(nameof(TooltipTextPaint));
                                OnPropertyChanged(nameof(TooltipBackgroundPaint));
                                OnPropertyChanged(nameof(Series));

                                // Transition to interactive mode
                                TransitionToInteractiveMode();

                                IsInitialized = true;
                            }
                            catch (Exception ex)
                            {
                                System.Diagnostics.Debug.WriteLine($"[CHART INIT] Error in UI thread: {ex.Message}");

                                // Hide skeleton loader on error
                                IsLoadingChart = false;
                                OnPropertyChanged(nameof(IsLoadingChart));
                            }
                        });
                    }
                    catch (Exception ex)
                    {
                        System.Diagnostics.Debug.WriteLine($"[CHART INIT] Error in background initialization: {ex.Message}");

                        // Hide skeleton loader on error
                        Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                        {
                            IsLoadingChart = false;
                            OnPropertyChanged(nameof(IsLoadingChart));
                        });
                    }
                });
                
                // Mark as initialized immediately (background chart loading continues)
                IsInitialized = true;
            }
            finally
            {
                IsInitializing = false;
            }
            
            return Task.CompletedTask;
        }


        private void EnsureInteractiveChartLoaded()
        {
            if (!IsInteractiveMode && !IsLoadingChart && !hasUserInteracted)
            {
                hasUserInteracted = true;
                // Force immediate transition to interactive mode
                _ = ForceInteractiveMode();
            }
        }

        private async Task ForceInteractiveMode()
        {
            if (IsInteractiveMode)
                return;

            IsLoadingChart = true;
            OnPropertyChanged(nameof(IsLoadingChart));

            // Load full resolution data for interactive use
            await Task.Run(() =>
            {
                SwitchToFullResolution();
            });

            Avalonia.Threading.Dispatcher.UIThread.Post(() =>
            {
                OnPropertyChanged(nameof(Series));
                TransitionToInteractiveMode();
            });
        }

        private void SwitchToFullResolution()
        {
            var xPoints = XCurvePreview?.Points?.ToArray() ?? Array.Empty<CurvePoint>();
            var yPoints = YCurvePreview?.Points?.ToArray() ?? Array.Empty<CurvePoint>();
            var hasYCurve = YXRatio.CurrentValidatedValue != 1.0;

            // Switch to full resolution by updating existing series
            if (cachedXSeries != null)
            {
                cachedXSeries.Values = xPoints;  // Full resolution
            }

            if (hasYCurve && cachedYSeries != null)
            {
                cachedYSeries.Values = yPoints;  // Full resolution
            }
        }

        private async void TransitionToInteractiveMode()
        {
            // Hide skeleton loader and show interactive chart
            IsLoadingChart = false;
            IsInteractiveMode = true;
            ChartOpacity = 1.0;

            OnPropertyChanged(nameof(IsLoadingChart));
            OnPropertyChanged(nameof(IsInteractiveMode));
            OnPropertyChanged(nameof(ChartOpacity));
        }

        public Task SwitchToProfileAsync(BE.IProfileModel profileModel)
        {
            if (currentProfileModel == profileModel && IsInitialized)
                return Task.CompletedTask;

            // Unsubscribe from old curve preview events
            if (XCurvePreview != null)
            {
                XCurvePreview.Points.CollectionChanged -= OnCurvePointsChanged;
            }
            if (YCurvePreview != null)
            {
                YCurvePreview.Points.CollectionChanged -= OnCurvePointsChanged;
            }

            currentProfileModel = profileModel;
            XCurvePreview = profileModel.XCurvePreview;
            YCurvePreview = profileModel.YCurvePreview;
            YXRatio = profileModel.YXRatio;

            YXRatio.PropertyChanged += OnYXRatioChanged;

            // Subscribe to new curve preview events
            XCurvePreview.Points.CollectionChanged += OnCurvePointsChanged;
            YCurvePreview.Points.CollectionChanged += OnCurvePointsChanged;

            // Update chart data synchronously for instant response
            CreateSeries();

            return Task.CompletedTask;
        }

        public ObservableCollection<ISeries> Series { get; set; } = new ObservableCollection<ISeries>();

        public Axis[] XAxes { get; set; } = new Axis[] { new Axis { Name = "Loading...", MinLimit = 0, MaxLimit = 1 } };

        public Axis[] YAxes { get; set; } = new Axis[] { new Axis { Name = "Loading...", MinLimit = 0, MaxLimit = 1 } };

        public SolidColorPaint TooltipTextPaint { get; set; } = new SolidColorPaint(SKColors.Black);

        public SolidColorPaint TooltipBackgroundPaint { get; set; } = new SolidColorPaint(SKColors.White);

        public ICommand RecreateAxesCommand { get; }

        public ICommand FitToDataCommand { get; }

        // ================================================================================================
        // PUBLIC METHODS
        // ================================================================================================

        public void FitToData()
        {
            var allPoints = XCurvePreview.Points.ToList();

            if (YXRatio.CurrentValidatedValue != 1.0)
            {
                allPoints.AddRange(YCurvePreview.Points);
            }

            if (allPoints.Count == 0)
            {
                SetDefaultLimits();
                return;
            }

            var (minX, maxX, minY, maxY) = CalculateDataBounds(allPoints);
            if (maxY == minY)
            {
                SetCenteredLimits(minX, maxX, minY, maxY);
            }
            else
            {
                SetPaddedLimits(minX, maxX, minY, maxY);
            }
        }

        public void RecreateAxes(double? xMinLimit = null, double? xMaxLimit = null, double? yMinLimit = null, double? yMaxLimit = null)
        {
            XAxes = CreateXAxes(xMinLimit, xMaxLimit);
            YAxes = CreateYAxes(yMinLimit, yMaxLimit);

            OnPropertyChanged(nameof(XAxes));
            OnPropertyChanged(nameof(YAxes));

            // Refresh dot positions after axis recreation (coordinates changed)
            if (ShowInputIndicator && lastMouseSpeed.HasValue)
            {
                UpdateInputIndicator(lastMouseSpeed.Value);
            }
        }

        // ================================================================================================
        // INPUT INDICATOR METHODS
        // ================================================================================================

        /// <summary>
        /// Sets the chart control reference for coordinate transformation
        /// </summary>
        public void SetChartControl(LiveChartsCore.SkiaSharpView.Avalonia.CartesianChart chart)
        {
            chartControl = chart;
            Console.WriteLine($"[ProfileChartViewModel] Chart control set: {chart != null}");
        }

        /// <summary>
        /// Shows or hides the input indicator dots
        /// </summary>
        public void SetInputIndicatorVisible(bool visible)
        {
            ShowInputIndicator = visible;
            if (visible)
            {
                OnPropertyChanged(nameof(ShowYCurveDot)); // Update Y dot visibility
            }
        }

        /// <summary>
        /// Enables or disables real-time indicator with monitoring service
        /// </summary>
        public void EnableRealtimeIndicator(bool enable)
        {
            ShowInputIndicator = enable;

            if (enable)
            {
                mouseInputMonitor.StartMonitoring();
            }
            else
            {
                mouseInputMonitor.StopMonitoring();
            }
        }

        private void OnMouseSpeedUpdated(object? sender, double mouseSpeed)
        {
            Console.WriteLine($"[ProfileChartViewModel] OnMouseSpeedUpdated called with speed: {mouseSpeed:F2}");
            Console.WriteLine($"[ProfileChartViewModel] ShowInputIndicator: {ShowInputIndicator}, IsInteractiveMode: {IsInteractiveMode}");

            // Dispatch to UI thread
            Avalonia.Threading.Dispatcher.UIThread.Post(() =>
            {
                if (ShowInputIndicator && IsInteractiveMode)
                {
                    UpdateInputIndicator(mouseSpeed);
                }
                else
                {
                    Console.WriteLine($"[ProfileChartViewModel] Skipping update - ShowInputIndicator: {ShowInputIndicator}, IsInteractiveMode: {IsInteractiveMode}");
                }
            });
        }

        /// <summary>
        /// Updates the input indicator dot position based on real-time mouse speed input
        /// </summary>
        /// <param name="currentMouseSpeed">Current mouse speed in counts/inch or mm</param>
        public void UpdateInputIndicator(double currentMouseSpeed)
        {
            Console.WriteLine($"[ProfileChartViewModel] UpdateInputIndicator called with speed: {currentMouseSpeed:F2}");
            Console.WriteLine($"[ProfileChartViewModel] IsInitialized: {IsInitialized}, IsInteractiveMode: {IsInteractiveMode}, chartControl != null: {chartControl != null}");

            if (!IsInitialized || !IsInteractiveMode || chartControl == null)
            {
                Console.WriteLine($"[ProfileChartViewModel] Skipping update - preconditions not met");
                return;
            }

            lastMouseSpeed = currentMouseSpeed; // Store for refresh on zoom/resize

            // Get Y values for the current X (mouse speed)
            var xCurveY = GetYValueForX(currentMouseSpeed, XCurvePreview.Points);
            Console.WriteLine($"[ProfileChartViewModel] X curve Y value: {xCurveY:F2}");

            // Convert data coordinates to pixel coordinates
            var (xPixelX, xPixelY) = DataToPixels(currentMouseSpeed, xCurveY);
            Console.WriteLine($"[ProfileChartViewModel] Pixel coordinates: ({xPixelX:F2}, {xPixelY:F2})");

            // Update X curve dot position
            XDotPixelX = xPixelX;
            XDotPixelY = xPixelY;
            Console.WriteLine($"[ProfileChartViewModel] Updated dot position to ({XDotPixelX:F2}, {XDotPixelY:F2})");

            // Update Y curve dot if visible
            if (YXRatio.CurrentValidatedValue != 1.0)
            {
                var yCurveY = GetYValueForX(currentMouseSpeed, YCurvePreview.Points);
                var (yPixelX, yPixelY) = DataToPixels(currentMouseSpeed, yCurveY);

                YDotPixelX = yPixelX;
                YDotPixelY = yPixelY;
                Console.WriteLine($"[ProfileChartViewModel] Updated Y dot position to ({YDotPixelX:F2}, {YDotPixelY:F2})");
            }
        }

        private (double pixelX, double pixelY) DataToPixels(double dataX, double dataY)
        {
            if (chartControl == null || XAxes == null || YAxes == null || XAxes.Length == 0 || YAxes.Length == 0)
            {
                Console.WriteLine($"[ProfileChartViewModel] DataToPixels: Chart or axes not initialized, returning (0, 0)");
                return (0, 0);
            }

            var xAxis = XAxes[0];
            var yAxis = YAxes[0];

            // Get axis limits
            var xMin = xAxis.MinLimit ?? 0;
            var xMax = xAxis.MaxLimit ?? 100;
            var yMin = yAxis.MinLimit ?? 0;
            var yMax = yAxis.MaxLimit ?? 2;

            Console.WriteLine($"[ProfileChartViewModel] DataToPixels: Axis limits: X[{xMin:F2}, {xMax:F2}], Y[{yMin:F2}, {yMax:F2}]");

            // Get chart bounds (approximate, accounting for margins)
            var chartWidth = chartControl.Bounds.Width;
            var chartHeight = chartControl.Bounds.Height;

            Console.WriteLine($"[ProfileChartViewModel] DataToPixels: Chart size: {chartWidth:F2} x {chartHeight:F2}");

            // Approximate margins (LiveCharts uses internal margins for axes)
            var marginLeft = 60.0;   // Space for Y-axis labels
            var marginRight = 20.0;
            var marginTop = 20.0;
            var marginBottom = 40.0; // Space for X-axis labels

            var plotWidth = chartWidth - marginLeft - marginRight;
            var plotHeight = chartHeight - marginTop - marginBottom;

            // Convert data coordinates to pixel coordinates
            var normalizedX = (dataX - xMin) / (xMax - xMin);
            var normalizedY = (dataY - yMin) / (yMax - yMin);

            Console.WriteLine($"[ProfileChartViewModel] DataToPixels: Normalized coords: ({normalizedX:F4}, {normalizedY:F4})");

            var pixelX = marginLeft + (normalizedX * plotWidth);
            var pixelY = marginTop + ((1.0 - normalizedY) * plotHeight); // Invert Y (screen coords go down)

            Console.WriteLine($"[ProfileChartViewModel] DataToPixels: Before offset: ({pixelX:F2}, {pixelY:F2})");

            // Check if coordinates are valid
            if (double.IsNaN(pixelX) || double.IsNaN(pixelY) ||
                double.IsInfinity(pixelX) || double.IsInfinity(pixelY))
            {
                Console.WriteLine("[ProfileChartViewModel] DataToPixels: Invalid pixel coordinates (NaN or Infinity)");
                return (0, 0);
            }

            // Adjust for dot center (dot is 12px, so offset by 6px)
            return (pixelX - 6, pixelY - 6);
        }

        private double GetYValueForX(double xValue, ObservableCollection<CurvePoint> curvePoints)
        {
            if (curvePoints.Count == 0)
            {
                Console.WriteLine($"[ProfileChartViewModel] GetYValueForX: No curve points, returning 0");
                return 0;
            }

            Console.WriteLine($"[ProfileChartViewModel] GetYValueForX: Finding Y for X={xValue:F2}, curve has {curvePoints.Count} points");

            // Find surrounding points
            CurvePoint? below = null;
            CurvePoint? above = null;

            for (int i = 0; i < curvePoints.Count; i++)
            {
                var point = curvePoints[i];
                if (point.MouseSpeed <= xValue)
                {
                    below = point;
                }
                else
                {
                    above = point;
                    break;
                }
            }

            // Handle edge cases
            if (below == null)
            {
                Console.WriteLine($"[ProfileChartViewModel] GetYValueForX: X below curve range, using first point: {curvePoints[0].Output:F2}");
                return curvePoints[0].Output;
            }
            if (above == null)
            {
                Console.WriteLine($"[ProfileChartViewModel] GetYValueForX: X above curve range, using last point: {curvePoints[curvePoints.Count - 1].Output:F2}");
                return curvePoints[curvePoints.Count - 1].Output;
            }

            // Linear interpolation between surrounding points
            var t = (xValue - below.MouseSpeed) / (above.MouseSpeed - below.MouseSpeed);
            var result = below.Output + t * (above.Output - below.Output);
            Console.WriteLine($"[ProfileChartViewModel] GetYValueForX: Interpolated between ({below.MouseSpeed:F2}, {below.Output:F2}) and ({above.MouseSpeed:F2}, {above.Output:F2}), result: {result:F2}");
            return result;
        }

        // ================================================================================================
        // CLEANUP & DISPOSAL
        // ================================================================================================

        public void Dispose()
        {
            // Stop monitoring and unsubscribe
            mouseInputMonitor.StopMonitoring();
            mouseInputMonitor.MouseSpeedUpdated -= OnMouseSpeedUpdated;

            themeService.ThemeChanged -= OnThemeChanged;
            localizationService.PropertyChanged -= OnLocalizationChanged;
            if (YXRatio != null)
                YXRatio.PropertyChanged -= OnYXRatioChanged;

            // Unsubscribe from curve preview events
            if (XCurvePreview != null)
                XCurvePreview.Points.CollectionChanged -= OnCurvePointsChanged;
            if (YCurvePreview != null)
                YCurvePreview.Points.CollectionChanged -= OnCurvePointsChanged;

            // Clear cached series references
            cachedXSeries = null;
            cachedYSeries = null;

            // Clear preview renderer cache for memory cleanup
            previewRenderer.ClearCache();
        }

        // ================================================================================================
        // CHART DATA MANAGEMENT
        // ================================================================================================

        private CurvePoint[] ReducePointsForPreview(CurvePoint[] points, int targetCount = 64)
        {
            if (points.Length <= targetCount)
                return points;

            // Use uniform sampling for consistent performance
            var step = (double)points.Length / targetCount;
            var reducedPoints = new CurvePoint[targetCount];
            
            for (int i = 0; i < targetCount; i++)
            {
                var sourceIndex = (int)(i * step);
                if (sourceIndex >= points.Length)
                    sourceIndex = points.Length - 1;
                
                reducedPoints[i] = points[sourceIndex];
            }
            
            return reducedPoints;
        }

        private LineSeries<CurvePoint> CreateOptimizedLineSeries(CurvePoint[] points, SolidColorPaint stroke, string name, string outputLabel)
        {
            return new LineSeries<CurvePoint>
            {
                Values = points,
                Fill = null,
                Stroke = stroke,
                Mapping = (curvePoint, index) => new LiveChartsCore.Kernel.Coordinate(x: curvePoint.MouseSpeed, y: curvePoint.Output),
                GeometrySize = 0,
                GeometryStroke = null,
                GeometryFill = null,
                AnimationsSpeed = TimeSpan.FromMilliseconds(100), // Reduced animations for performance
                Name = name,
                LineSmoothness = 0, // Disable smoothing for better performance
                XToolTipLabelFormatter = (chartPoint) => $"Speed: {chartPoint.Coordinate.SecondaryValue:F2}",
                YToolTipLabelFormatter = (chartPoint) => $"{outputLabel}: {chartPoint.Coordinate.PrimaryValue:F2}"
            };
        }

        private void CreateSeries()
        {
            // Get fresh data
            var xPoints = XCurvePreview?.Points?.ToArray() ?? Array.Empty<CurvePoint>();
            var yPoints = YCurvePreview?.Points?.ToArray() ?? Array.Empty<CurvePoint>();
            var reducedXPoints = ReducePointsForPreview(xPoints, 64);
            var reducedYPoints = ReducePointsForPreview(yPoints, 64);

            var hasYCurve = YXRatio.CurrentValidatedValue != 1.0;

            // Create X series if needed, otherwise update
            if (cachedXSeries == null)
            {
                var xStroke = new SolidColorPaint(SKColors.CornflowerBlue) { StrokeThickness = MainStrokeThickness };
                cachedXSeries = CreateOptimizedLineSeries(reducedXPoints, xStroke, "X Curve Profile", "X Output");
                Series.Add(cachedXSeries);
            }
            else
            {
                // Update existing series data (triggers animation!)
                cachedXSeries.Values = reducedXPoints;
            }

            // Handle Y series based on Y/X ratio
            if (hasYCurve)
            {
                if (cachedYSeries == null)
                {
                    var yStroke = new SolidColorPaint(SKColors.OrangeRed) { StrokeThickness = MainStrokeThickness };
                    cachedYSeries = CreateOptimizedLineSeries(reducedYPoints, yStroke, "Y Curve Profile", "Y Output");
                    Series.Add(cachedYSeries);
                }
                else
                {
                    cachedYSeries.Values = reducedYPoints;
                }
            }
            else
            {
                // Remove Y series if Y/X ratio is 1.0
                if (cachedYSeries != null)
                {
                    Series.Remove(cachedYSeries);
                    cachedYSeries = null;
                }
            }
        }

        // ================================================================================================
        // EVENT HANDLERS
        // ================================================================================================

        private void OnYXRatioChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EditableSetting<double>.CurrentValidatedValue))
            {
                CreateSeries();
                OnPropertyChanged(nameof(Series));

                // Update Y dot visibility when ratio changes
                OnPropertyChanged(nameof(ShowYCurveDot));

                // Refresh dot positions for new curve
                if (ShowInputIndicator && lastMouseSpeed.HasValue)
                {
                    UpdateInputIndicator(lastMouseSpeed.Value);
                }
            }
        }

        private void OnCurvePointsChanged(object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
        {
            // Post to UI thread with Background priority to naturally debounce rapid updates
            // SetPoints() fires Clear + N Add events; this batches them into one update
            Avalonia.Threading.Dispatcher.UIThread.Post(() =>
            {
                CreateSeries();
                OnPropertyChanged(nameof(Series));
            }, Avalonia.Threading.DispatcherPriority.Background);
        }

        // ================================================================================================
        // CHART AXES CREATION
        // ================================================================================================

        private Axis[] CreateXAxes(double? minLimit = null, double? maxLimit = null)
        {
            var axisName = localizationService?.GetText("ChartAxisMouseSpeed") ?? "Mouse Speed";
            var titleColor = themeService.GetCachedColor(AxisTitleBrush);
            var labelColor = themeService.GetCachedColor(AxisLabelsBrush);
            var separatorColor = themeService.GetCachedColor(AxisSeparatorsBrush);

            return new Axis[]
            {
                new Axis()
                {
                    Name = axisName,
                    NameTextSize = AxisNameTextSize,
                    NamePaint = new SolidColorPaint(titleColor),
                    LabelsPaint = new SolidColorPaint(labelColor),
                    TextSize = AxisTextSize,
                    SeparatorsPaint = new SolidColorPaint(separatorColor) { StrokeThickness = StandardStrokeThickness },
                    TicksPaint = new SolidColorPaint(titleColor) { StrokeThickness = StandardStrokeThickness },
                    SubseparatorsPaint = new SolidColorPaint(separatorColor.WithAlpha(SubSeparatorAlpha)) { StrokeThickness = SubStrokeThickness },
                    AnimationsSpeed = TimeSpan.FromMilliseconds(100),
                    MinLimit = minLimit ?? 0,
                    MaxLimit = maxLimit
                }
            };
        }

        private Axis[] CreateYAxes(double? minLimit = null, double? maxLimit = null)
        {
            var axisName = localizationService?.GetText("ChartAxisOutput") ?? "Output";
            var titleColor = themeService.GetCachedColor(AxisTitleBrush);
            var labelColor = themeService.GetCachedColor(AxisLabelsBrush);
            var separatorColor = themeService.GetCachedColor(AxisSeparatorsBrush);

            return new Axis[]
            {
                new Axis()
                {
                    Name = axisName,
                    NameTextSize = AxisNameTextSize,
                    NamePaint = new SolidColorPaint(titleColor),
                    LabelsPaint = new SolidColorPaint(labelColor),
                    TextSize = AxisTextSize,
                    SeparatorsPaint = new SolidColorPaint(separatorColor) { StrokeThickness = StandardStrokeThickness },
                    TicksPaint = new SolidColorPaint(titleColor) { StrokeThickness = StandardStrokeThickness },
                    SubseparatorsPaint = new SolidColorPaint(separatorColor.WithAlpha(SubSeparatorAlpha)) { StrokeThickness = SubStrokeThickness },
                    AnimationsSpeed = TimeSpan.FromMilliseconds(100),
                    MinLimit = minLimit ?? 0,
                    MaxLimit = maxLimit
                }
            };
        }


        // ================================================================================================
        // AXIS LIMITS MANAGEMENT
        // ================================================================================================

        private (double xMin, double xMax, double yMin, double yMax) CalculateInitialAxisLimits()
        {
            var allPoints = XCurvePreview.Points.ToList();

            if (YXRatio.CurrentValidatedValue != 1.0)
            {
                allPoints.AddRange(YCurvePreview.Points);
            }

            if (allPoints.Count == 0)
            {
                // Return default limits
                return (0, DefaultMaxX, 0, DefaultMaxY);
            }

            var (minX, maxX, minY, maxY) = CalculateDataBounds(allPoints);

            if (maxY == minY)
            {
                // Return centered limits
                var centerY = (minY + maxY) / 2;
                var centerX = (minX + maxX) / 2;
                return (
                    Math.Max(0, centerX - DefaultAxisRange),
                    centerX + DefaultAxisRange,
                    Math.Max(0, centerY - DefaultYRange),
                    centerY + DefaultYRange
                );
            }
            else
            {
                // Return padded limits
                var xRange = maxX - minX;
                var yRange = maxY - minY;
                var xPadding = xRange * DataPaddingRatio;
                var yPadding = yRange * DataPaddingRatio;
                return (
                    Math.Max(0, minX - xPadding),
                    maxX + xPadding,
                    Math.Max(0, minY - yPadding),
                    maxY + yPadding
                );
            }
        }

        private void SetDefaultLimits()
        {
            XAxes[0].MinLimit = 0;
            XAxes[0].MaxLimit = DefaultMaxX;
            YAxes[0].MinLimit = 0;
            YAxes[0].MaxLimit = DefaultMaxY;
        }

        private static (double minX, double maxX, double minY, double maxY) CalculateDataBounds(System.Collections.Generic.List<CurvePoint> points)
        {
            var minX = points.Min(p => p.MouseSpeed);
            var maxX = points.Max(p => p.MouseSpeed);
            var minY = points.Min(p => p.Output);
            var maxY = points.Max(p => p.Output);
            return (minX, maxX, minY, maxY);
        }

        private void SetCenteredLimits(double minX, double maxX, double minY, double maxY)
        {
            var centerY = (minY + maxY) / 2;
            var centerX = (minX + maxX) / 2;
            YAxes[0].MinLimit = Math.Max(0, centerY - DefaultYRange);
            YAxes[0].MaxLimit = centerY + DefaultYRange;
            XAxes[0].MinLimit = Math.Max(0, centerX - DefaultAxisRange);
            XAxes[0].MaxLimit = centerX + DefaultAxisRange;
        }

        private void SetPaddedLimits(double minX, double maxX, double minY, double maxY)
        {
            var xRange = maxX - minX;
            var yRange = maxY - minY;
            var xPadding = xRange * DataPaddingRatio;
            var yPadding = yRange * DataPaddingRatio;
            XAxes[0].MinLimit = Math.Max(0, minX - xPadding);
            XAxes[0].MaxLimit = maxX + xPadding;
            YAxes[0].MinLimit = Math.Max(0, minY - yPadding);
            YAxes[0].MaxLimit = maxY + yPadding;
        }

        private void OnThemeChanged(object? sender, EventArgs e)
        {
            TooltipTextPaint.Color = themeService.GetCachedColor(AxisTitleBrush);
            TooltipBackgroundPaint.Color = themeService.GetCachedColor(TooltipBackgroundBrush).WithAlpha(TooltipBackgroundAlpha);

            var currentXMin = XAxes?[0]?.MinLimit;
            var currentXMax = XAxes?[0]?.MaxLimit;
            var currentYMin = YAxes?[0]?.MinLimit;
            var currentYMax = YAxes?[0]?.MaxLimit;

            RecreateAxes(currentXMin, currentXMax, currentYMin, currentYMax);

            // Notify tooltip property changes
            OnPropertyChanged(nameof(TooltipTextPaint));
            OnPropertyChanged(nameof(TooltipBackgroundPaint));
        }

        private void OnLocalizationChanged(object? sender, PropertyChangedEventArgs e)
        {
            // Recreate axes with current limits but updated localized names
            var currentXMin = XAxes?[0]?.MinLimit;
            var currentXMax = XAxes?[0]?.MaxLimit;
            var currentYMin = YAxes?[0]?.MinLimit;
            var currentYMax = YAxes?[0]?.MaxLimit;

            RecreateAxes(currentXMin, currentXMax, currentYMin, currentYMax);
        }
    }
}