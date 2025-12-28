using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using System.Linq;
using userinterface.ViewModels.Profile;
using userinterface.Views.Controls;

namespace userinterface.Views.Profile;

public partial class AccelerationProfileSettingsView : UserControl
{
    private const int NoneAccelerationIndex = 0;
    private const int FormulaAccelerationIndex = 1;
    private const int LUTAccelerationIndex = 2;
    private const double ViewContainerTopMargin = 8.0;

    private Grid? AccelerationTypeGrid;
    private ContentControl? FormulaViewContainer;
    private ContentControl? LUTViewContainer;
    private LocalizedComboBox? AccelerationComboBox;
    private AnisotropyProfileSettingsView? AnisotropyView;
    private CoalescionProfileSettingsView? CoalescionView;

    public AccelerationProfileSettingsView()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnLoaded(object? sender, RoutedEventArgs e)
    {
        if (AccelerationTypeGrid == null)
        {
            SetupControls();
        }
    }

    private void SetupControls()
    {
        if (DataContext is not AccelerationProfileSettingsViewModel viewModel)
            return;

        CreateAccelerationComboBox(viewModel);
        CreateAccelerationTypeGrid();
        CreateViewContainers();
        AddControlsToMainPanel(viewModel);
        UpdateViewBasedOnSelection();
    }

    private void CreateAccelerationComboBox(AccelerationProfileSettingsViewModel viewModel)
    {
        AccelerationComboBox = new LocalizedComboBox
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Center,
            LocalizationKeys = AccelerationProfileSettingsViewModel.DefinitionTypeKeysLocal,
            EnumValues = AccelerationProfileSettingsViewModel.DefinitionTypesLocal
        };

        AccelerationComboBox.SelectionChanged += (s, e) =>
        {
            if (AccelerationComboBox.SelectedEnumValue != null)
            {
                viewModel.AccelerationBE.DefinitionType.InterfaceValue = AccelerationComboBox.SelectedEnumValue;
                viewModel.AccelerationBE.DefinitionType.TryUpdateFromInterface();
                UpdateViewBasedOnSelection();
            }
        };

        var currentValue = viewModel.AccelerationBE.DefinitionType.InterfaceValue;
        if (!string.IsNullOrEmpty(currentValue))
        {
            var matchingItem = AccelerationComboBox.localizedItems.FirstOrDefault(item => item.EnumValue == currentValue);
            if (matchingItem != null)
            {
                AccelerationComboBox.SelectedItem = matchingItem;
            }
        }

        AccelerationComboBox.RefreshItems();
    }

    private void CreateAccelerationTypeGrid()
    {
        if (AccelerationComboBox == null)
            return;

        AccelerationTypeGrid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            RowDefinitions = new RowDefinitions("Auto"),
            ColumnSpacing = 16
        };

        var label = new TextBlock
        {
            Text = "Type",
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Avalonia.Media.Brushes.Gray
        };
        Grid.SetColumn(label, 0);
        Grid.SetRow(label, 0);

        Grid.SetColumn(AccelerationComboBox, 1);
        Grid.SetRow(AccelerationComboBox, 0);

        AccelerationTypeGrid.Children.Add(label);
        AccelerationTypeGrid.Children.Add(AccelerationComboBox);
    }

    private void CreateViewContainers()
    {
        var containerMargin = new Thickness(0, ViewContainerTopMargin, 0, 0);

        FormulaViewContainer = new ContentControl
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Margin = containerMargin,
            IsVisible = false
        };

        LUTViewContainer = new ContentControl
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Margin = containerMargin,
            IsVisible = false
        };
    }

    private void AddControlsToMainPanel(AccelerationProfileSettingsViewModel viewModel)
    {
        var mainStackPanel = this.FindControl<StackPanel>("MainStackPanel");
        if (mainStackPanel == null || AccelerationTypeGrid == null ||
            FormulaViewContainer == null || LUTViewContainer == null)
            return;

        mainStackPanel.Children.Insert(0, AccelerationTypeGrid);
        mainStackPanel.Children.Insert(1, FormulaViewContainer);
        mainStackPanel.Children.Insert(2, LUTViewContainer);

        AnisotropyView = new AnisotropyProfileSettingsView
        {
            DataContext = viewModel.AnisotropySettings,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            IsVisible = false
        };

        CoalescionView = new CoalescionProfileSettingsView
        {
            DataContext = viewModel.CoalescionSettings,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            IsVisible = false
        };

        mainStackPanel.Children.Add(AnisotropyView);
        mainStackPanel.Children.Add(CoalescionView);
    }

    private void UpdateViewBasedOnSelection()
    {
        if (DataContext is not AccelerationProfileSettingsViewModel viewModel || AccelerationComboBox == null)
            return;

        var selectedValue = AccelerationComboBox.SelectedEnumValue;
        var selectedIndex = AccelerationProfileSettingsViewModel.DefinitionTypesLocal.ToList().IndexOf(selectedValue ?? "");
        var isNotNone = selectedIndex != NoneAccelerationIndex;

        HideAllViews();
        UpdateAdditionalFieldsVisibility(isNotNone);

        switch (selectedIndex)
        {
            case NoneAccelerationIndex:
                break;

            case FormulaAccelerationIndex:
                ShowFormulaView(viewModel);
                break;

            case LUTAccelerationIndex:
                ShowLUTView(viewModel);
                break;
        }
    }

    private void UpdateAdditionalFieldsVisibility(bool isVisible)
    {
        if (AnisotropyView != null)
            AnisotropyView.IsVisible = isVisible;
        if (CoalescionView != null)
            CoalescionView.IsVisible = isVisible;
    }

    private void HideAllViews()
    {
        if (FormulaViewContainer != null)
            FormulaViewContainer.IsVisible = false;
        if (LUTViewContainer != null)
            LUTViewContainer.IsVisible = false;
    }

    private void ShowFormulaView(AccelerationProfileSettingsViewModel viewModel)
    {
        if (FormulaViewContainer == null)
            return;

        var formulaView = new AccelerationFormulaSettingsView
        {
            DataContext = viewModel.AccelerationFormulaSettings,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        FormulaViewContainer.Content = formulaView;
        FormulaViewContainer.IsVisible = true;
    }

    private void ShowLUTView(AccelerationProfileSettingsViewModel viewModel)
    {
        if (LUTViewContainer == null)
            return;

        var lutView = new AccelerationLUTSettingsView
        {
            DataContext = viewModel.AccelerationLUTSettings,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        LUTViewContainer.Content = lutView;
        LUTViewContainer.IsVisible = true;
    }
}
