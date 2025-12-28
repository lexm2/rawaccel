using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Layout;
using System.Linq;
using userinterface.ViewModels.Settings;
using userinterface.Views.Controls;

namespace userinterface.Views.Settings;

public partial class GeneralSettingsView : UserControl
{
    public GeneralSettingsView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, System.EventArgs e)
    {
        if (DataContext is GeneralSettingsViewModel generalSettingsViewModel)
        {
            SetupSettings(generalSettingsViewModel);
        }
    }

    private void SetupSettings(GeneralSettingsViewModel generalSettingsViewModel)
    {
        SettingsStackPanel.Children.Clear();

        // Language row
        SettingsStackPanel.Children.Add(CreateLanguageRow(generalSettingsViewModel));

        // Theme row
        SettingsStackPanel.Children.Add(CreateThemeRow(generalSettingsViewModel));

        // Toast notifications row
        SettingsStackPanel.Children.Add(CreateCheckboxRow("SettingsShowToastNotifications", generalSettingsViewModel.NotificationSettings, "ShowToastNotifications"));

        // Confirm modals row
        SettingsStackPanel.Children.Add(CreateCheckboxRow("SettingsShowConfirmModals", generalSettingsViewModel.NotificationSettings, "ShowConfirmModals"));
    }

    private static Grid CreateLanguageRow(GeneralSettingsViewModel viewModel)
    {
        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            ColumnSpacing = 16,
            Margin = new Avalonia.Thickness(0, 0, 0, 12)
        };

        var label = new TextBlock
        {
            Text = "Language",
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Avalonia.Media.Brushes.Gray
        };
        Grid.SetColumn(label, 0);

        var languageComboBox = new ComboBox
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Center,
            DataContext = viewModel
        };
        languageComboBox.Bind(ComboBox.ItemsSourceProperty, new Binding("AvailableLanguages"));
        languageComboBox.Bind(ComboBox.SelectedItemProperty, new Binding("SelectedLanguage"));
        languageComboBox.DisplayMemberBinding = new Binding("DisplayName");
        Grid.SetColumn(languageComboBox, 1);

        grid.Children.Add(label);
        grid.Children.Add(languageComboBox);

        return grid;
    }

    private Grid CreateThemeRow(GeneralSettingsViewModel viewModel)
    {
        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            ColumnSpacing = 16,
            Margin = new Avalonia.Thickness(0, 0, 0, 12)
        };

        var label = new TextBlock
        {
            Text = "Theme",
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Avalonia.Media.Brushes.Gray
        };
        Grid.SetColumn(label, 0);

        var themeComboBox = new LocalizedComboBox
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Center,
            DataContext = viewModel
        };
        themeComboBox.Bind(LocalizedComboBox.LocalizationKeysProperty, new Binding("ThemeLocalizationKeys"));
        themeComboBox.Bind(LocalizedComboBox.EnumValuesProperty, new Binding("ThemeEnumValues"));

        themeComboBox.SelectionChanged += (sender, e) =>
        {
            if (themeComboBox.SelectedEnumValue != null)
            {
                viewModel.SelectedThemeValue = themeComboBox.SelectedEnumValue;
            }
        };

        themeComboBox.Loaded += (sender, e) =>
        {
            UpdateThemeSelection(themeComboBox, viewModel);
        };

        viewModel.PropertyChanged += (sender, e) =>
        {
            if (e.PropertyName == nameof(GeneralSettingsViewModel.SelectedThemeValue))
            {
                UpdateThemeSelection(themeComboBox, viewModel);
            }
        };

        Grid.SetColumn(themeComboBox, 1);

        grid.Children.Add(label);
        grid.Children.Add(themeComboBox);

        return grid;
    }

    private static Grid CreateCheckboxRow(string labelKey, object dataContext, string bindingPath)
    {
        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            ColumnSpacing = 16,
            Margin = new Avalonia.Thickness(0, 0, 0, 12)
        };

        var label = new TextBlock
        {
            Text = labelKey, // TODO: Localize
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Avalonia.Media.Brushes.Gray
        };
        Grid.SetColumn(label, 0);

        var checkBox = new CheckBox
        {
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Center,
            DataContext = dataContext
        };
        checkBox.Bind(CheckBox.IsCheckedProperty, new Binding(bindingPath));
        Grid.SetColumn(checkBox, 1);

        grid.Children.Add(label);
        grid.Children.Add(checkBox);

        return grid;
    }

    private static void UpdateThemeSelection(LocalizedComboBox themeComboBox, GeneralSettingsViewModel viewModel)
    {
        if (!string.IsNullOrEmpty(viewModel.SelectedThemeValue) && themeComboBox.localizedItems?.Any() == true)
        {
            var targetItem = themeComboBox.localizedItems.FirstOrDefault(item => item.EnumValue == viewModel.SelectedThemeValue);
            if (targetItem != null && themeComboBox.SelectedItem != targetItem)
            {
                themeComboBox.SelectedItem = targetItem;
            }
        }
    }
}
