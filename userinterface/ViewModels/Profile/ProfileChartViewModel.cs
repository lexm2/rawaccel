using userinterface.Charting.Axes;
using userinterface.Charting.Core;
using userinterface.Charting.Extensions;
using userinterface.Charting.Interfaces;
using userinterface.Charting.Painting;
using userinterface.Charting.Series;
using Microsoft.Extensions.DependencyInjection;
using SkiaSharp;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Interfaces;
using userinterface.Services;
using userspace_backend;
using userspace_backend.Display;
using userspace_backend.Logging;
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
        private const int DefaultMaxX = 500;
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

        // Axis labeling and text
        private const int AxisNameTextSize = 14;
        private const int AxisTextSize = 12;


        private readonly IThemeService themeService;
        private readonly ILocalizationService localizationService;
        private readonly IPreviewChartRenderer previewRenderer;
        private readonly IBackEnd backEnd;
        private readonly ILoggingService loggingService;
        private BE.IProfileModel currentProfileModel = null!;

        private SolidColorPaint? cachedXStroke;
        private SolidColorPaint? cachedYStroke;

        private LineSeries<CurvePoint>? xSeries;
        private LineSeries<CurvePoint>? ySeries;
        private ScatterSeries<CurvePoint>? currentSpeedDotSeries;
        private ScatterSeries<CurvePoint>? currentYSpeedDotSeries;
        private LoggingScatterSeries<CurvePoint>? xLUTDotSeries;
        private LoggingScatterSeries<CurvePoint>? yLUTDotSeries;

        private readonly object syncObject = new object();

        public ProfileChartViewModel(IThemeService themeService, ILocalizationService localizationService, IPreviewChartRenderer previewRenderer, IBackEnd backEnd, ILoggingService loggingService)
        {
            this.themeService = themeService ?? throw new ArgumentNullException(nameof(themeService));
            this.localizationService = localizationService ?? throw new ArgumentNullException(nameof(localizationService));
            this.previewRenderer = previewRenderer ?? throw new ArgumentNullException(nameof(previewRenderer));
            this.backEnd = backEnd ?? throw new ArgumentNullException(nameof(backEnd));
            this.loggingService = loggingService ?? throw new ArgumentNullException(nameof(loggingService));

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
            ToggleRealTimeTrackingCommand = new RelayCommand(ToggleRealTimeTracking);
            ToggleDriverGraphApproximationCommand = new RelayCommand(ToggleDriverGraphApproximation);
        }

        public bool IsInitialized { get; private set; }

        public bool IsInitializing { get; private set; }

        public bool IsInteractiveMode { get; private set; } = false;

        public bool IsLoadingChart { get; private set; } = false;

        public double ChartOpacity { get; private set; } = 0.0;

        private bool hasUserInteracted = false;

        public bool IsRealTimeTrackingEnabled { get; private set; } = false;

        public bool ShowDriverGraphApproximation { get; private set; } = false;

        public string CurrentMouseDevice { get; private set; } = "No device detected";

        public string CurrentDeviceDPI { get; private set; } = "Unknown DPI";

        private readonly ObservableCollection<CurvePoint> currentSpeedData = new ObservableCollection<CurvePoint>();
        private readonly ObservableCollection<CurvePoint> currentYSpeedData = new ObservableCollection<CurvePoint>();

        private double maxXAxisLimit = 0;
        private double maxYAxisLimit = 0;
        private double currentMaxXData = 0;
        private double currentMaxYData = 0;


        private ICurvePreview CurvePreview { get; set; } = null!;

        private IEditableSettingSpecific<double> YXRatio { get; set; } = null!;

        public void Initialize(BE.IProfileModel profileModel)
        {
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.Initialize: Starting for profile '{ProfileName}'",
                profileModel?.CurrentNameForDisplay ?? "null");

            if (currentProfileModel == profileModel)
            {
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.Initialize: Same profile, skipping");
                return;
            }

            // Unsubscribe from previous events
            if (currentProfileModel != null)
            {
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.Initialize: Unsubscribing from previous profile '{PreviousProfile}'",
                    currentProfileModel.CurrentNameForDisplay);
                UnsubscribeFromEvents();
            }

            currentProfileModel = profileModel;
            CurvePreview = profileModel.CurvePreview;
            YXRatio = profileModel.YXRatio;

            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.Initialize: CurvePreview has {PointCount} points",
                CurvePreview?.Points?.Count ?? 0);

            if (CurvePreview?.Points != null)
            {
                InitializeSeries();
            }

            SubscribeToEvents();
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.Initialize: Complete for profile '{ProfileName}'",
                profileModel.CurrentNameForDisplay);
        }


        // ================================================================================================
        // INITIALIZATION & SETUP
        // ================================================================================================

        public Task InitializeAsync()
        {
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeAsync: Called (IsInitializing={IsInitializing}, IsInitialized={IsInitialized}, Profile={ProfileName})",
                IsInitializing, IsInitialized, currentProfileModel?.CurrentNameForDisplay ?? "null");

            // Validate chart state before proceeding
            ValidateChartState();

            if (IsInitializing || IsInitialized || currentProfileModel == null)
            {
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeAsync: Skipping initialization");
                return Task.CompletedTask;
            }

            IsInitializing = true;

            try
            {
                IsLoadingChart = true;
                OnPropertyChanged(nameof(IsLoadingChart));
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeAsync: Starting background initialization");

                _ = Task.Run(async () =>
                {
                    try
                    {
                        await Task.Delay(100);
                        await Task.Run(() =>
                        {
                            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeAsync: Initializing series on background thread");
                            InitializeSeries();
                        });

                        Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                        {
                            try
                            {
                                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeAsync: Creating axes and UI elements on UI thread");

                                XAxes = CreateXAxes();
                                YAxes = CreateYAxes();
                                TooltipTextPaint = new SolidColorPaint(themeService.GetCachedColor(AxisTitleBrush));
                                TooltipBackgroundPaint = new SolidColorPaint(themeService.GetCachedColor(TooltipBackgroundBrush).WithAlpha(TooltipBackgroundAlpha));

                                this.themeService.ThemeChanged += OnThemeChanged;
                                this.localizationService.PropertyChanged += OnLocalizationChanged;

                                OnPropertyChanged(nameof(XAxes));
                                OnPropertyChanged(nameof(YAxes));
                                OnPropertyChanged(nameof(TooltipTextPaint));
                                OnPropertyChanged(nameof(TooltipBackgroundPaint));
                                OnPropertyChanged(nameof(Series));

                                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeAsync: Transitioning to interactive mode");
                                TransitionToInteractiveMode();

                            }
                            catch (Exception ex)
                            {
                                loggingService.LogError(LogSource.UI, ex, "ProfileChartViewModel.InitializeAsync: Error during UI thread initialization");
                                IsLoadingChart = false;
                                OnPropertyChanged(nameof(IsLoadingChart));
                            }
                        });
                    }
                    catch (Exception ex)
                    {
                        loggingService.LogError(LogSource.UI, ex, "ProfileChartViewModel.InitializeAsync: Error during background initialization");
                        Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                        {
                            IsLoadingChart = false;
                            OnPropertyChanged(nameof(IsLoadingChart));
                        });
                    }
                });

                IsInitialized = true;
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeAsync: Initialization started successfully");
            }
            finally
            {
                IsInitializing = false;
            }

            return Task.CompletedTask;
        }


        private void ValidateChartState()
        {
            bool hasIssues = false;

            // Check if curve preview has valid data
            if (CurvePreview == null || CurvePreview.Points == null)
            {
                loggingService.LogWarning(LogSource.UI, "ProfileChartViewModel.ValidateChartState: CurvePreview or Points is null");
                hasIssues = true;
            }
            else if (CurvePreview.Points.Count == 0)
            {
                loggingService.LogWarning(LogSource.UI, "ProfileChartViewModel.ValidateChartState: CurvePreview has 0 points");
                hasIssues = true;
            }

            // Check series bindings
            if (xSeries != null)
            {
                if (xSeries.Values != CurvePreview?.Points)
                {
                    loggingService.LogWarning(LogSource.UI, "ProfileChartViewModel.ValidateChartState: xSeries values not bound to CurvePreview.Points, rebinding");
                    xSeries.Values = CurvePreview?.Points;
                    hasIssues = true;
                }
            }

            if (ySeries != null)
            {
                if (ySeries.Values != CurvePreview?.Points)
                {
                    loggingService.LogWarning(LogSource.UI, "ProfileChartViewModel.ValidateChartState: ySeries values not bound to CurvePreview.Points, rebinding");
                    ySeries.Values = CurvePreview?.Points;
                    hasIssues = true;
                }
            }

            // Check visibility state consistency
            if (IsInitialized && !IsInteractiveMode)
            {
                loggingService.LogWarning(LogSource.UI, "ProfileChartViewModel.ValidateChartState: Chart is initialized but not interactive, fixing");
                IsInteractiveMode = true;
                OnPropertyChanged(nameof(IsInteractiveMode));
                hasIssues = true;
            }

            if (IsInteractiveMode && ChartOpacity < 0.5)
            {
                loggingService.LogWarning(LogSource.UI, "ProfileChartViewModel.ValidateChartState: Chart is interactive but opacity is {Opacity}, fixing", ChartOpacity);
                ChartOpacity = 1.0;
                OnPropertyChanged(nameof(ChartOpacity));
                hasIssues = true;
            }

            if (!hasIssues)
            {
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.ValidateChartState: Chart state is valid");
            }
            else
            {
                loggingService.LogInformation(LogSource.UI, "ProfileChartViewModel.ValidateChartState: Fixed chart state issues");
            }
        }

        private void EnsureInteractiveChartLoaded()
        {
            if (!IsInteractiveMode && !IsLoadingChart && !hasUserInteracted)
            {
                hasUserInteracted = true;
                _ = ForceInteractiveMode();
            }
        }

        private Task ForceInteractiveMode()
        {
            if (IsInteractiveMode)
                return Task.CompletedTask;

            IsLoadingChart = true;
            OnPropertyChanged(nameof(IsLoadingChart));

            // LiveCharts automatically uses the full ObservableCollection data
            Avalonia.Threading.Dispatcher.UIThread.Post(() =>
            {
                OnPropertyChanged(nameof(Series));
                TransitionToInteractiveMode();
            });

            return Task.CompletedTask;
        }

        private void InitializeSeries()
        {
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeSeries: Starting with {PointCount} curve points",
                CurvePreview?.Points?.Count ?? 0);

            if (cachedXStroke == null)
                cachedXStroke = new SolidColorPaint(SKColors.CornflowerBlue) { StrokeThickness = MainStrokeThickness };
            if (cachedYStroke == null)
                cachedYStroke = new SolidColorPaint(SKColors.OrangeRed) { StrokeThickness = MainStrokeThickness };

            xSeries = CreateLineSeries(CurvePreview.Points, cachedXStroke, "Curve Profile", "Curve");
            ySeries = CreateLineSeries(CurvePreview.Points, cachedYStroke, "Curve Profile Y", "Y");

            Series.Clear();
            Series.Add(xSeries);

            InitializeCurrentSpeedDotSeries();
            InitializeLUTDotSeries();
            UpdateYSeriesVisibility();
            UpdateLineSeriesGeometry();
            UpdateLUTDotsVisibility();

            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.InitializeSeries: Complete, {SeriesCount} series in collection",
                Series.Count);
        }

        private LineSeries<CurvePoint> CreateLineSeries(ObservableCollection<CurvePoint> points, SolidColorPaint stroke, string name, string axis)
        {
            return new LineSeries<CurvePoint>
            {
                Values = points,
                Fill = null,
                Stroke = stroke,
                Mapping = (object curvePointObj, int index) =>
                {
                    var curvePoint = (CurvePoint)curvePointObj;
                    return new ChartPoint(curvePoint.MouseSpeed, curvePoint.Output);
                },
                GeometrySize = 0,
                GeometryStroke = null,
                GeometryFill = null,
                AnimationsSpeed = TimeSpan.FromMilliseconds(100),
                Name = name,
                LineSmoothness = 0,
                XToolTipLabelFormatter = (chartPoint) => $"Speed: {chartPoint.X:F2}",
                YToolTipLabelFormatter = (chartPoint) => $"{axis} Output: {chartPoint.Y:F2}"
            };
        }

        private void UpdateLineSeriesGeometry()
        {
            if (xSeries == null || ySeries == null || currentProfileModel == null) return;

            if (ShowDriverGraphApproximation)
            {
                // When toggle is on, show dots on the line series for any acceleration mode
                // These dots represent the driver's approximation of the curve
                xSeries.GeometrySize = 5;
                xSeries.GeometryFill = new SolidColorPaint(SKColors.CornflowerBlue);
                xSeries.GeometryStroke = new SolidColorPaint(SKColors.DarkBlue) { StrokeThickness = 1 };

                ySeries.GeometrySize = 5;
                ySeries.GeometryFill = new SolidColorPaint(SKColors.OrangeRed);
                ySeries.GeometryStroke = new SolidColorPaint(SKColors.DarkRed) { StrokeThickness = 1 };
            }
            else
            {
                // Hide dots when toggle is off
                xSeries.GeometrySize = 0;
                xSeries.GeometryFill = null;
                xSeries.GeometryStroke = null;

                ySeries.GeometrySize = 0;
                ySeries.GeometryFill = null;
                ySeries.GeometryStroke = null;
            }
        }

        private void ToggleDriverGraphApproximation()
        {
            ShowDriverGraphApproximation = !ShowDriverGraphApproximation;
            UpdateLineSeriesGeometry();
            OnPropertyChanged(nameof(ShowDriverGraphApproximation));
        }

        private async void TransitionToInteractiveMode()
        {
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.TransitionToInteractiveMode: Starting fade-in animation");

            IsLoadingChart = false;
            IsInteractiveMode = true;
            ChartOpacity = 0.0;

            // Batch property changes
            OnPropertyChanged(nameof(IsLoadingChart));
            OnPropertyChanged(nameof(IsInteractiveMode));
            OnPropertyChanged(nameof(ChartOpacity));

            await Task.Delay(100);

            ChartOpacity = 1.0;
            OnPropertyChanged(nameof(ChartOpacity));

            loggingService.LogInformation(LogSource.UI, "ProfileChartViewModel: Chart is now interactive and visible");
        }

        public Task SwitchToProfileAsync(BE.IProfileModel profileModel)
        {
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.SwitchToProfileAsync: Switching to profile '{ProfileName}'",
                profileModel?.CurrentNameForDisplay ?? "null");

            if (currentProfileModel == profileModel && IsInitialized)
            {
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.SwitchToProfileAsync: Same profile and already initialized, skipping");
                return Task.CompletedTask;
            }

            if (currentProfileModel != null)
            {
                loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.SwitchToProfileAsync: Unsubscribing from previous profile '{PreviousProfile}'",
                    currentProfileModel.CurrentNameForDisplay);
                UnsubscribeFromEvents();
            }

            currentProfileModel = profileModel;
            CurvePreview = profileModel.CurvePreview;
            YXRatio = profileModel.YXRatio;

            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.SwitchToProfileAsync: New profile has {PointCount} curve points",
                CurvePreview?.Points?.Count ?? 0);

            SubscribeToEvents();
            if (xSeries != null && ySeries != null)
            {
                UpdateYSeriesVisibility();
                UpdateLineSeriesGeometry();
            }

            // Update LUT dots data for new profile
            // TODO: Extract LUT points from profileModel.Acceleration.LookupTableAccel.Data.ModelValue.Data
            // The Data array contains alternating X,Y pairs that need to be split
            if (xLUTDotSeries != null)
            {
                // xLUTDotSeries.Values = ExtractXLUTPoints(profileModel);
            }
            if (yLUTDotSeries != null)
            {
                // yLUTDotSeries.Values = ExtractYLUTPoints(profileModel);
            }
            UpdateLUTDotsVisibility();

            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.SwitchToProfileAsync: Profile switch complete");
            return Task.CompletedTask;
        }

        public ObservableCollection<ISeries> Series { get; set; } = new ObservableCollection<ISeries>();

        public Axis[] XAxes { get; set; } = new Axis[] { new Axis { Name = "Loading...", MinLimit = 0, MaxLimit = 1 } };

        public Axis[] YAxes { get; set; } = new Axis[] { new Axis { Name = "Loading...", MinLimit = 0, MaxLimit = 1 } };

        public SolidColorPaint TooltipTextPaint { get; set; } = new SolidColorPaint(SKColors.Black);

        public SolidColorPaint TooltipBackgroundPaint { get; set; } = new SolidColorPaint(SKColors.White);

        public ICommand RecreateAxesCommand { get; }

        public ICommand FitToDataCommand { get; }

        public ICommand ToggleRealTimeTrackingCommand { get; }

        public ICommand ToggleDriverGraphApproximationCommand { get; }

        // ================================================================================================
        // PUBLIC METHODS
        // ================================================================================================

        public void FitToData()
        {
            var allPoints = CurvePreview.Points.ToList();

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

            // Batch property changes
            OnPropertyChanged(nameof(XAxes));
            OnPropertyChanged(nameof(YAxes));
        }

        // ================================================================================================
        // CLEANUP & DISPOSAL
        // ================================================================================================

        public void Dispose()
        {
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.Dispose: Cleaning up resources for profile '{ProfileName}'",
                currentProfileModel?.CurrentNameForDisplay ?? "null");

            StopRealTimeTracking();

            themeService.ThemeChanged -= OnThemeChanged;
            localizationService.PropertyChanged -= OnLocalizationChanged;
            UnsubscribeFromEvents();

            cachedXStroke = null;
            cachedYStroke = null;

            previewRenderer.ClearCache();
            loggingService.LogDebug(LogSource.UI, "ProfileChartViewModel.Dispose: Complete");
        }

        // ================================================================================================
        // EVENT HANDLERS
        // ================================================================================================

        private void SubscribeToEvents()
        {
            if (YXRatio != null)
                YXRatio.PropertyChanged += OnYXRatioChanged;

            if (currentProfileModel?.Acceleration?.Selection != null)
                currentProfileModel.Acceleration.Selection.PropertyChanged += OnAccelerationTypeChanged;
        }

        private void UnsubscribeFromEvents()
        {
            if (YXRatio != null)
                YXRatio.PropertyChanged -= OnYXRatioChanged;

            if (currentProfileModel?.Acceleration?.Selection != null)
                currentProfileModel.Acceleration.Selection.PropertyChanged -= OnAccelerationTypeChanged;
        }

        private void OnYXRatioChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EditableSetting<double>.ModelValue))
            {
                UpdateYSeriesVisibility();
            }
        }

        private void UpdateYSeriesVisibility()
        {
            if (ySeries == null) return;

            var hasYCurve = YXRatio.ModelValue != 1.0;
            var ySeriesExists = Series.Contains(ySeries);

            if (hasYCurve && !ySeriesExists)
            {
                Series.Add(ySeries);
            }
            else if (!hasYCurve && ySeriesExists)
            {
                Series.Remove(ySeries);
            }

            // Update Y speed dot visibility based on curve separation
            if (currentYSpeedDotSeries != null && IsRealTimeTrackingEnabled)
            {
                currentYSpeedDotSeries.IsVisible = hasYCurve;
            }

            // Also update line series geometry when Y series visibility changes
            UpdateLineSeriesGeometry();
            
            // Also update LUT dots visibility when Y series visibility changes
            UpdateLUTDotsVisibility();
        }

        private void OnAccelerationTypeChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EditableSetting<userspace_backend.Data.Profiles.Acceleration.AccelerationDefinitionType>.ModelValue))
            {
                // Update geometry when acceleration type changes (dots may appear different for different curve types)
                UpdateLineSeriesGeometry();

                // Update LUT dots visibility when acceleration type changes
                UpdateLUTDotsVisibility();

                // Update LUT points data when switching to/from LUT mode
                // TODO: Extract LUT points from currentProfileModel.Acceleration.LookupTableAccel.Data
                if (xLUTDotSeries != null && currentProfileModel != null)
                {
                    // xLUTDotSeries.Values = ExtractXLUTPoints(currentProfileModel);
                }
                if (yLUTDotSeries != null && currentProfileModel != null)
                {
                    // yLUTDotSeries.Values = ExtractYLUTPoints(currentProfileModel);
                }
            }
        }

        // ================================================================================================
        // CHART AXES CREATION
        // ================================================================================================

        private Axis[] CreateXAxes(double? minLimit = null, double? maxLimit = null)
        {
            var axisName = localizationService?.GetText("ChartAxisMouseSpeed") ?? "Mouse Speed";
            return CreateAxis(axisName, minLimit, maxLimit);
        }

        private Axis[] CreateYAxes(double? minLimit = null, double? maxLimit = null)
        {
            var axisName = localizationService?.GetText("ChartAxisOutput") ?? "Output";
            return CreateAxis(axisName, minLimit, maxLimit);
        }

        private Axis[] CreateAxis(string name, double? minLimit, double? maxLimit)
        {
            var titleColor = themeService.GetCachedColor(AxisTitleBrush);
            var labelColor = themeService.GetCachedColor(AxisLabelsBrush);
            var separatorColor = themeService.GetCachedColor(AxisSeparatorsBrush);

            return new Axis[]
            {
                new Axis()
                {
                    Name = name,
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

        private void SetDefaultLimits()
        {
            // Don't allow axis limits to shrink below stored maximums
            XAxes[0].MinLimit = 0;
            XAxes[0].MaxLimit = Math.Max(maxXAxisLimit, DefaultMaxX);
            YAxes[0].MinLimit = 0;
            YAxes[0].MaxLimit = Math.Max(maxYAxisLimit, DefaultMaxY);

            // Update stored maximums if they increased
            maxXAxisLimit = XAxes[0].MaxLimit ?? maxXAxisLimit;
            maxYAxisLimit = YAxes[0].MaxLimit ?? maxYAxisLimit;
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

            // Don't allow axis limits to shrink below stored maximums
            YAxes[0].MinLimit = Math.Max(0, centerY - DefaultYRange);
            YAxes[0].MaxLimit = Math.Max(maxYAxisLimit, centerY + DefaultYRange);
            XAxes[0].MinLimit = Math.Max(0, centerX - DefaultAxisRange);
            XAxes[0].MaxLimit = Math.Max(maxXAxisLimit, centerX + DefaultAxisRange);

            // Update stored maximums if they increased
            maxXAxisLimit = XAxes[0].MaxLimit ?? maxXAxisLimit;
            maxYAxisLimit = YAxes[0].MaxLimit ?? maxYAxisLimit;
        }

        private void SetPaddedLimits(double minX, double maxX, double minY, double maxY)
        {
            var xRange = maxX - minX;
            var yRange = maxY - minY;
            var xPadding = xRange * DataPaddingRatio;
            var yPadding = yRange * DataPaddingRatio;

            // Don't allow axis limits to shrink below stored maximums
            XAxes[0].MinLimit = Math.Max(0, minX - xPadding);
            XAxes[0].MaxLimit = Math.Max(maxXAxisLimit, maxX + xPadding);
            YAxes[0].MinLimit = Math.Max(0, minY - yPadding);
            YAxes[0].MaxLimit = Math.Max(maxYAxisLimit, maxY + yPadding);

            // Update stored maximums if they increased
            maxXAxisLimit = XAxes[0].MaxLimit ?? maxXAxisLimit;
            maxYAxisLimit = YAxes[0].MaxLimit ?? maxYAxisLimit;
            currentMaxXData = Math.Max(currentMaxXData, maxX);
            currentMaxYData = Math.Max(currentMaxYData, maxY);
        }


        private void OnThemeChanged(object? sender, EventArgs e)
        {
            TooltipTextPaint.Color = themeService.GetCachedColor(AxisTitleBrush);
            TooltipBackgroundPaint.Color = themeService.GetCachedColor(TooltipBackgroundBrush).WithAlpha(TooltipBackgroundAlpha);

            var accentColor = themeService.GetCachedColor("SecondaryAccentBrush");
            if (currentSpeedDotSeries != null)
            {
                if (currentSpeedDotSeries.Stroke is SolidColorPaint strokePaint)
                {
                    strokePaint.Color = accentColor;
                }
                if (currentSpeedDotSeries.Fill is SolidColorPaint fillPaint)
                {
                    fillPaint.Color = accentColor;
                }
            }
            if (currentYSpeedDotSeries != null)
            {
                if (currentYSpeedDotSeries.Stroke is SolidColorPaint yStrokePaint)
                {
                    yStrokePaint.Color = accentColor;
                }
                if (currentYSpeedDotSeries.Fill is SolidColorPaint yFillPaint)
                {
                    yFillPaint.Color = accentColor;
                }
            }

            var currentXMin = XAxes?[0]?.MinLimit;
            var currentXMax = maxXAxisLimit;
            var currentYMin = YAxes?[0]?.MinLimit;
            var currentYMax = maxYAxisLimit;

            RecreateAxes(currentXMin, currentXMax, currentYMin, currentYMax);

            // Batch property changes for theme updates
            OnPropertyChanged(nameof(TooltipTextPaint));
            OnPropertyChanged(nameof(TooltipBackgroundPaint));
        }

        private void OnLocalizationChanged(object? sender, PropertyChangedEventArgs e)
        {
            var currentXMin = XAxes?[0]?.MinLimit;
            var currentXMax = maxXAxisLimit;
            var currentYMin = YAxes?[0]?.MinLimit;
            var currentYMax = maxYAxisLimit;

            RecreateAxes(currentXMin, currentXMax, currentYMin, currentYMax);
        }

        private void InitializeCurrentSpeedDotSeries()
        {
            var accentColor = themeService.GetCachedColor("SecondaryAccentBrush");

            if (currentSpeedDotSeries == null)
            {
                currentSpeedDotSeries = CreateSpeedDotSeries(currentSpeedData, "Current X Speed", accentColor);
            }

            if (currentYSpeedDotSeries == null)
            {
                currentYSpeedDotSeries = CreateSpeedDotSeries(currentYSpeedData, "Current Y Speed", accentColor);
            }

            // Always ensure they're in the series collection after a clear
            if (!Series.Contains(currentSpeedDotSeries))
            {
                Series.Add(currentSpeedDotSeries);
            }
            if (!Series.Contains(currentYSpeedDotSeries))
            {
                Series.Add(currentYSpeedDotSeries);
            }
        }

        private ScatterSeries<CurvePoint> CreateSpeedDotSeries(ObservableCollection<CurvePoint> data, string name, SKColor color)
        {
            return new ScatterSeries<CurvePoint>
            {
                Values = data,
                GeometrySize = 8,
                Stroke = new SolidColorPaint(color) { StrokeThickness = 2 },
                Fill = new SolidColorPaint(color),
                Mapping = (object curvePointObj, int index) =>
                {
                    var curvePoint = (CurvePoint)curvePointObj;
                    return new ChartPoint(curvePoint.MouseSpeed, curvePoint.Output);
                },
                Name = name,
                IsVisible = false,
                DataPadding = new ChartPadding(0, 0)
            };
        }

        private void InitializeLUTDotSeries()
        {
            if (currentProfileModel == null) return;

            // Use injected logging service for LUT point click logging

            // Create scatter series for X LUT points
            // TODO: Extract LUT points from currentProfileModel.Acceleration.LookupTableAccel.Data
            xLUTDotSeries = new LoggingScatterSeries<CurvePoint>()
            {
                Values = null, // currentProfileModel.XLUTPoints removed - need to extract from LookupTableAccel.Data
                GeometrySize = 8,
                Stroke = new SolidColorPaint(SKColors.DarkBlue) { StrokeThickness = 2 },
                Fill = new SolidColorPaint(SKColors.LightBlue),
                Mapping = (object curvePointObj, int index) =>
                {
                    var curvePoint = (CurvePoint)curvePointObj;
                    return new ChartPoint(curvePoint.MouseSpeed, curvePoint.Output);
                },
                Name = "X LUT Points",
                IsVisible = false,
                DataPadding = new ChartPadding(0, 0)
            };

            // Create scatter series for Y LUT points
            yLUTDotSeries = new LoggingScatterSeries<CurvePoint>()
            {
                Values = null, // currentProfileModel.YLUTPoints removed - need to extract from LookupTableAccel.Data
                GeometrySize = 8,
                Stroke = new SolidColorPaint(SKColors.DarkRed) { StrokeThickness = 2 },
                Fill = new SolidColorPaint(SKColors.LightPink),
                Mapping = (object curvePointObj, int index) =>
                {
                    var curvePoint = (CurvePoint)curvePointObj;
                    return new ChartPoint(curvePoint.MouseSpeed, curvePoint.Output);
                },
                Name = "Y LUT Points",
                IsVisible = false,
                DataPadding = new ChartPadding(0, 0)
            };

            // Subscribe to click events
            xLUTDotSeries.PointClicked += (point) =>
            {
                loggingService?.LogInformation(LogSource.LUT,
                    "X LUT Point clicked on graph - X: {XValue}, Y: {YValue}",
                    point.X,
                    point.Y);
            };

            yLUTDotSeries.PointClicked += (point) =>
            {
                loggingService?.LogInformation(LogSource.LUT,
                    "Y LUT Point clicked on graph - X: {XValue}, Y: {YValue}",
                    point.X,
                    point.Y);
            };

            // Add to series collection
            if (!Series.Contains(xLUTDotSeries))
            {
                Series.Add(xLUTDotSeries);
            }
            if (!Series.Contains(yLUTDotSeries))
            {
                Series.Add(yLUTDotSeries);
            }
        }

        private void UpdateLUTDotsVisibility()
        {
            if (xLUTDotSeries == null || yLUTDotSeries == null || currentProfileModel == null) return;

            // Check if current acceleration type is LUT
            var isLUT = currentProfileModel.Acceleration?.Selection?.ModelValue ==
                userspace_backend.Data.Profiles.Acceleration.AccelerationDefinitionType.LookupTable;

            xLUTDotSeries.IsVisible = isLUT;

            // Y LUT dots are visible only if LUT and Y curve is separate
            var hasYCurve = YXRatio.ModelValue != 1.0;
            yLUTDotSeries.IsVisible = isLUT && hasYCurve;
        }

        public void HandleChartClick(userinterface.Charting.Controls.CartesianChart chart, double pixelX, double pixelY)
        {
            if (xLUTDotSeries == null || yLUTDotSeries == null || !xLUTDotSeries.IsVisible)
                return;

            var clickPoint = new SKPoint((float)pixelX, (float)pixelY);

            // Check each LUT series with pixel-based distance calculation
            CheckLUTPointHitsPixelBased(chart, xLUTDotSeries, clickPoint, "X");
            if (yLUTDotSeries.IsVisible)
            {
                CheckLUTPointHitsPixelBased(chart, yLUTDotSeries, clickPoint, "Y");
            }
        }

        private void CheckLUTPointHitsPixelBased(userinterface.Charting.Controls.CartesianChart chart,
                                                LoggingScatterSeries<CurvePoint>? series, 
                                                SKPoint clickPixels, 
                                                string seriesType)
        {
            if (series?.Values is not IEnumerable<CurvePoint> points || !series.IsVisible)
                return;

            double pixelTolerance = 100.0; // 100 pixels as requested
            
            var pointList = points.ToList();
            for (int i = 0; i < pointList.Count; i++)
            {
                var point = pointList[i];
                
                // Convert LUT point data coordinates to pixel coordinates
                var pointDataCoord = new ChartPoint(point.MouseSpeed, point.Output);
                var pointPixels = chart.ScaleDataToPixels(pointDataCoord);

                // Calculate pixel distance between click and point
                var pixelDistance = Math.Sqrt(
                    Math.Pow(clickPixels.X - pointPixels.X, 2) +
                    Math.Pow(clickPixels.Y - pointPixels.Y, 2)
                );

                if (pixelDistance <= pixelTolerance)
                {
                    series.LogPointClick(pointDataCoord);
                }
            }
        }

        private void ToggleRealTimeTracking()
        {
            if (IsRealTimeTrackingEnabled)
            {
                StopRealTimeTracking();
            }
            else
            {
                StartRealTimeTracking();
            }
        }

        private void StartRealTimeTracking()
        {
            if (IsRealTimeTrackingEnabled) return;

            IsRealTimeTrackingEnabled = true;

            currentSpeedData.Clear();
            currentYSpeedData.Clear();

            var hasYCurve = YXRatio.ModelValue != 1.0;

            // Hardware tracking disabled - Hardware folder removed
            // TODO: Re-implement if needed without Hardware dependencies

            if (currentSpeedDotSeries != null)
            {
                currentSpeedDotSeries.IsVisible = false;
            }
            if (currentYSpeedDotSeries != null)
            {
                currentYSpeedDotSeries.IsVisible = false;
            }


            OnPropertyChanged(nameof(IsRealTimeTrackingEnabled));
        }

        private BE.IDeviceModel? GetActiveDeviceModel()
        {
            // For now, get the first device or return null to use default DPI
            // TODO: Implement proper active device detection based on current mapping
            return backEnd.Devices.Elements.FirstOrDefault();
        }

        private void StopRealTimeTracking()
        {
            if (!IsRealTimeTrackingEnabled) return;

            IsRealTimeTrackingEnabled = false;

            // Hardware tracking disabled - Hardware folder removed

            currentSpeedData.Clear();
            currentYSpeedData.Clear();

            CurrentMouseDevice = "No device detected";
            CurrentDeviceDPI = "Unknown DPI";

            // Batch property changes
            OnPropertyChanged(nameof(CurrentMouseDevice));
            OnPropertyChanged(nameof(CurrentDeviceDPI));


            if (currentSpeedDotSeries != null)
            {
                currentSpeedDotSeries.IsVisible = false;
            }
            if (currentYSpeedDotSeries != null)
            {
                currentYSpeedDotSeries.IsVisible = false;
            }

            OnPropertyChanged(nameof(IsRealTimeTrackingEnabled));
        }

        // OnMouseMoved handler removed - Hardware functionality disabled
        // TODO: Re-implement if needed without Hardware dependencies
        private void OnMouseMoved(object? sender, EventArgs e)
        {
            // Hardware tracking disabled
        }

        private double? InterpolateOutputFromSpeed(double mouseSpeed, ICurvePreview curvePreview)
        {
            if (curvePreview?.Points == null || curvePreview.Points.Count == 0)
                return null;

            var points = curvePreview.Points.ToList();

            // Find the closest points for interpolation
            var lowerPoint = points.LastOrDefault(p => p.MouseSpeed <= mouseSpeed);
            var upperPoint = points.FirstOrDefault(p => p.MouseSpeed >= mouseSpeed);

            if (lowerPoint == null && upperPoint == null)
                return null;

            if (lowerPoint == null)
                return upperPoint!.Output;

            if (upperPoint == null)
                return lowerPoint.Output;

            if (Math.Abs(lowerPoint.MouseSpeed - upperPoint.MouseSpeed) < 0.001)
                return lowerPoint.Output;

            // Linear interpolation
            double ratio = (mouseSpeed - lowerPoint.MouseSpeed) / (upperPoint.MouseSpeed - lowerPoint.MouseSpeed);
            return lowerPoint.Output + ratio * (upperPoint.Output - lowerPoint.Output);
        }

        private void UpdateCurrentSpeedDots(double xSpeed, double? xOutputValue, double ySpeed, double? yOutputValue, bool hasYCurve)
        {
            if (!IsRealTimeTrackingEnabled) return;

            // Update X speed dot position (keep it persistent, just update position)
            if (xOutputValue.HasValue && xSpeed > 0)
            {
                if (currentSpeedData.Count == 0)
                {
                    currentSpeedData.Add(new CurvePoint { MouseSpeed = xSpeed, Output = xOutputValue.Value });
                }
                else
                {
                    currentSpeedData[0].MouseSpeed = xSpeed;
                    currentSpeedData[0].Output = xOutputValue.Value;
                }
            }

            // Update Y speed dot position (only when separate curves)
            if (hasYCurve && yOutputValue.HasValue && ySpeed > 0)
            {
                if (currentYSpeedData.Count == 0)
                {
                    currentYSpeedData.Add(new CurvePoint { MouseSpeed = ySpeed, Output = yOutputValue.Value });
                }
                else
                {
                    currentYSpeedData[0].MouseSpeed = ySpeed;
                    currentYSpeedData[0].Output = yOutputValue.Value;
                }
            }

            // Update dot visibility based on curve separation
            if (currentSpeedDotSeries != null)
            {
                currentSpeedDotSeries.IsVisible = IsRealTimeTrackingEnabled;
            }
            if (currentYSpeedDotSeries != null)
            {
                currentYSpeedDotSeries.IsVisible = IsRealTimeTrackingEnabled && hasYCurve;
            }

            // Track maximum data values for axis expansion
            if (xSpeed > currentMaxXData || ySpeed > currentMaxXData)
            {
                currentMaxXData = Math.Max(xSpeed, ySpeed);
            }

            if (xOutputValue.HasValue && xOutputValue.Value > currentMaxYData)
            {
                currentMaxYData = xOutputValue.Value;
            }

            if (yOutputValue.HasValue && yOutputValue.Value > currentMaxYData)
            {
                currentMaxYData = yOutputValue.Value;
            }
        }

        // OnMouseIdle handler removed - Hardware functionality disabled
        private void OnMouseIdle(object? sender, EventArgs e)
        {
            if (!IsRealTimeTrackingEnabled) return;

            Avalonia.Threading.Dispatcher.UIThread.Post(() =>
            {
                currentSpeedData.Clear();
                currentYSpeedData.Clear();
            });
        }
    }
}