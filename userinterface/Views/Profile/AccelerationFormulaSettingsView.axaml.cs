using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using userinterface.ViewModels.Controls;
using userinterface.ViewModels.Profile;
using userinterface.Views.Controls;
using BEData = userspace_backend.Data.Profiles.Accel.FormulaAccel;

namespace userinterface.Views.Profile;

public partial class AccelerationFormulaSettingsView : UserControl
{
    private Grid? FormulaTypeGrid;
    private StackPanel? FieldsContainer;
    private LocalizedComboBox? FormulaTypeCombo;

    public AccelerationFormulaSettingsView()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnLoaded(object? sender, RoutedEventArgs e)
    {
        if (FormulaTypeGrid == null)
        {
            SetupControls();
        }
    }

    private void SetupControls()
    {
        if (DataContext is not AccelerationFormulaSettingsViewModel viewModel)
        {
            return;
        }

        CreateFormulaTypeComboBox();

        if (FormulaTypeCombo == null)
        {
            return;
        }

        CreateFormulaTypeGrid();
        CreateFieldsContainer();
        AddControlsToStackPanel();

        UpdateFormulaFields(viewModel.FormulaAccelBE.FormulaType.ModelValue, viewModel);
    }

    private void CreateFormulaTypeComboBox()
    {
        if (DataContext is not AccelerationFormulaSettingsViewModel viewModel)
            return;

        FormulaTypeCombo = new LocalizedComboBox
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Center,
            LocalizationKeys = AccelerationFormulaSettingsViewModel.FormulaTypeKeysLocal,
            EnumValues = AccelerationFormulaSettingsViewModel.FormulaTypesLocal
        };

        FormulaTypeCombo.RefreshItems();

        // Set initial selection to match backend value BEFORE attaching handler
        FormulaTypeCombo.SetSelectedValue(viewModel.FormulaAccelBE.FormulaType.InterfaceValue);

        // NOW attach the handler - only fires for future user changes
        FormulaTypeCombo.SelectionChanged += (s, e) =>
        {
            if (DataContext is AccelerationFormulaSettingsViewModel vm && FormulaTypeCombo.SelectedEnumValue != null)
            {
                vm.FormulaAccelBE.FormulaType.InterfaceValue = FormulaTypeCombo.SelectedEnumValue;
                vm.FormulaAccelBE.FormulaType.TryUpdateFromInterface();
                OnFormulaTypeSelectionChanged();
            }
        };
    }

    private void CreateFormulaTypeGrid()
    {
        if (FormulaTypeCombo == null)
            return;

        FormulaTypeGrid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            RowDefinitions = new RowDefinitions("Auto"),
            ColumnSpacing = 16
        };

        var label = new TextBlock
        {
            Text = "Formula",
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Avalonia.Media.Brushes.Gray
        };
        Grid.SetColumn(label, 0);
        Grid.SetRow(label, 0);

        Grid.SetColumn(FormulaTypeCombo, 1);
        Grid.SetRow(FormulaTypeCombo, 0);

        FormulaTypeGrid.Children.Add(label);
        FormulaTypeGrid.Children.Add(FormulaTypeCombo);
    }

    private void CreateFieldsContainer()
    {
        FieldsContainer = new StackPanel
        {
            Spacing = 12,
            Margin = new Avalonia.Thickness(0, 12, 0, 0)
        };
    }

    private void AddControlsToStackPanel()
    {
        var accelStackPanel = this.FindControl<StackPanel>("AccelStackPanel");
        if (accelStackPanel == null || FormulaTypeGrid == null || FieldsContainer == null)
            return;

        accelStackPanel.Children.Add(FormulaTypeGrid);
        accelStackPanel.Children.Add(FieldsContainer);
    }

    private void OnFormulaTypeSelectionChanged()
    {
        if (DataContext is not AccelerationFormulaSettingsViewModel viewModel)
        {
            return;
        }

        UpdateFormulaFields(viewModel.FormulaAccelBE.FormulaType.ModelValue, viewModel);
    }

    private void UpdateFormulaFields(BEData.AccelerationFormulaType formulaType, AccelerationFormulaSettingsViewModel formulaSettings)
    {
        if (FieldsContainer == null)
            return;

        FieldsContainer.Children.Clear();

        switch (formulaType)
        {
            case BEData.AccelerationFormulaType.Synchronous:
                AddSynchronousFields(formulaSettings);
                break;

            case BEData.AccelerationFormulaType.Linear:
                AddLinearFields(formulaSettings);
                break;

            case BEData.AccelerationFormulaType.Classic:
                AddClassicFields(formulaSettings);
                break;

            case BEData.AccelerationFormulaType.Power:
                AddPowerFields(formulaSettings);
                break;

            case BEData.AccelerationFormulaType.Natural:
                AddNaturalFields(formulaSettings);
                break;

            case BEData.AccelerationFormulaType.Jump:
                AddJumpFields(formulaSettings);
                break;
        }
    }

    private void AddSynchronousFields(AccelerationFormulaSettingsViewModel formulaSettings)
    {
        FieldsContainer!.Children.Add(CreateFieldRow(formulaSettings.SynchronousSettings.SyncSpeed));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.SynchronousSettings.Motivity));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.SynchronousSettings.Gamma));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.SynchronousSettings.Smoothness));
    }

    private void AddLinearFields(AccelerationFormulaSettingsViewModel formulaSettings)
    {
        FieldsContainer!.Children.Add(CreateFieldRow(formulaSettings.LinearSettings.Acceleration));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.LinearSettings.Offset));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.LinearSettings.Cap));
    }

    private void AddClassicFields(AccelerationFormulaSettingsViewModel formulaSettings)
    {
        FieldsContainer!.Children.Add(CreateFieldRow(formulaSettings.ClassicSettings.Acceleration));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.ClassicSettings.Exponent));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.ClassicSettings.Offset));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.ClassicSettings.Cap));
    }

    private void AddPowerFields(AccelerationFormulaSettingsViewModel formulaSettings)
    {
        FieldsContainer!.Children.Add(CreateFieldRow(formulaSettings.PowerSettings.Scale));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.PowerSettings.Exponent));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.PowerSettings.OutputOffset));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.PowerSettings.Cap));
    }

    private void AddNaturalFields(AccelerationFormulaSettingsViewModel formulaSettings)
    {
        FieldsContainer!.Children.Add(CreateFieldRow(formulaSettings.NaturalSettings.DecayRate));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.NaturalSettings.InputOffset));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.NaturalSettings.Limit));
    }

    private void AddJumpFields(AccelerationFormulaSettingsViewModel formulaSettings)
    {
        FieldsContainer!.Children.Add(CreateFieldRow(formulaSettings.JumpSettings.Smooth));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.JumpSettings.Input));
        FieldsContainer.Children.Add(CreateFieldRow(formulaSettings.JumpSettings.Output));
    }

    private static Grid CreateFieldRow(LocalizedFieldBase field)
    {
        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            ColumnSpacing = 16
        };

        var label = new TextBlock
        {
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Avalonia.Media.Brushes.Gray
        };
        label.Bind(TextBlock.TextProperty, new Avalonia.Data.Binding("Label") { Source = field });
        Grid.SetColumn(label, 0);

        var content = new ContentControl
        {
            Content = field,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };
        Grid.SetColumn(content, 1);

        grid.Children.Add(label);
        grid.Children.Add(content);

        return grid;
    }
}
