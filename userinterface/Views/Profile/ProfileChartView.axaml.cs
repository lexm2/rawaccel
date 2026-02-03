using Avalonia.Controls;
using System;
using System.Diagnostics;
using userinterface.ViewModels.Profile;

namespace userinterface.Views.Profile;

public partial class ProfileChartView : UserControl
{
    private bool isChartInitialized = false;

    public ProfileChartView()
    {
        InitializeComponent();
        AttachedToVisualTree += OnAttachedToVisualTree;
    }

    private async void OnAttachedToVisualTree(object? sender, Avalonia.VisualTreeAttachmentEventArgs e)
    {
        Console.WriteLine("[ProfileChartView] OnAttachedToVisualTree called");

        if (isChartInitialized || DataContext is not ProfileChartViewModel viewModel)
        {
            Console.WriteLine($"[ProfileChartView] Skipping - isChartInitialized: {isChartInitialized}, DataContext is ProfileChartViewModel: {DataContext is ProfileChartViewModel}");
            return;
        }

        isChartInitialized = true;

        try
        {
            Console.WriteLine($"[ProfileChartView] Chart control: {Chart != null}");

            // Pass chart reference to ViewModel for coordinate transformation
            viewModel.SetChartControl(Chart);

            if (!viewModel.IsInitialized)
            {
                Console.WriteLine("[ProfileChartView] Initializing ViewModel");
                await viewModel.InitializeAsync();
            }
            else
            {
                Console.WriteLine("[ProfileChartView] ViewModel already initialized");
            }
        }
        catch (System.Exception ex)
        {
            Console.WriteLine($"[ProfileChartView] Error during initialization: {ex.Message}");
            Debug.WriteLine($"[CHART INIT] Error during initialization: {ex.Message}");
        }
    }
}