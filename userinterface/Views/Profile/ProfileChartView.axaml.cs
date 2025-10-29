using Avalonia.Controls;
using Avalonia.Input;
using userinterface.Charting.Controls;
using userinterface.ViewModels.Profile;
using userspace_backend.Logging;
using System;

namespace userinterface.Views.Profile;

public partial class ProfileChartView : UserControl
{
    private bool isChartInitialized = false;
    private ProfileChartViewModel? currentViewModel = null;
    private readonly ILoggingService? loggingService;

    public ProfileChartView()
    {
        InitializeComponent();

        // Try to get logging service from App.Services
        loggingService = App.Services?.GetService(typeof(ILoggingService)) as ILoggingService;

        AttachedToVisualTree += OnAttachedToVisualTree;
        DetachedFromVisualTree += OnDetachedFromVisualTree;
        DataContextChanged += OnDataContextChanged;

        loggingService?.LogDebug(LogSource.UI, "ProfileChartView: Constructor called");
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        var newViewModel = DataContext as ProfileChartViewModel;

        loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnDataContextChanged: Old VM={OldVM}, New VM={NewVM}, isChartInitialized={IsInitialized}",
            currentViewModel != null ? "exists" : "null",
            newViewModel != null ? "exists" : "null",
            isChartInitialized);

        // Reset initialization flag when ViewModel changes
        if (currentViewModel != newViewModel)
        {
            loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnDataContextChanged: ViewModel changed, resetting isChartInitialized flag");
            isChartInitialized = false;
            currentViewModel = newViewModel;
        }
    }

    private void OnDetachedFromVisualTree(object? sender, Avalonia.VisualTreeAttachmentEventArgs e)
    {
        loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnDetachedFromVisualTree: View detached from visual tree");
    }

    private async void OnAttachedToVisualTree(object? sender, Avalonia.VisualTreeAttachmentEventArgs e)
    {
        loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnAttachedToVisualTree: View attached, isChartInitialized={IsInitialized}, DataContext={HasContext}",
            isChartInitialized,
            DataContext != null ? "exists" : "null");

        if (isChartInitialized || DataContext is not ProfileChartViewModel viewModel)
        {
            loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnAttachedToVisualTree: Skipping initialization");
            return;
        }

        isChartInitialized = true;
        loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnAttachedToVisualTree: Starting chart initialization");

        try
        {
            if (!viewModel.IsInitialized)
            {
                loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnAttachedToVisualTree: Calling viewModel.InitializeAsync()");
                await viewModel.InitializeAsync();
            }
            else
            {
                loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnAttachedToVisualTree: ViewModel already initialized");
            }

            // Add click handler to the chart
            var chart = this.FindControl<CartesianChart>("Chart");
            if (chart != null)
            {
                chart.PointerPressed += OnChartPointerPressed;
                loggingService?.LogDebug(LogSource.UI, "ProfileChartView.OnAttachedToVisualTree: Chart click handler registered");
            }
        }
        catch (System.Exception ex)
        {
            loggingService?.LogError(LogSource.UI, ex, "ProfileChartView.OnAttachedToVisualTree: Error during chart initialization");
        }
    }

    private void OnChartPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not CartesianChart chart || DataContext is not ProfileChartViewModel viewModel)
            return;

        var position = e.GetPosition(chart);
        viewModel.HandleChartClick(chart, position.X, position.Y);
    }
}