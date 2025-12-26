using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Collections.ObjectModel;
using userinterface.Services;
using BE = userspace_backend.Model.EditableSettings;

namespace userinterface.ViewModels.Controls;

public partial class DualColumnLabelFieldViewModel : ViewModelBase
{
    private const double DefaultLabelWidth = 120.0;

    [ObservableProperty]
    private double labelWidth = DefaultLabelWidth;

    private readonly LocalizationService localizationService;

    public ObservableCollection<FieldItemViewModel> Fields { get; }

    public DualColumnLabelFieldViewModel(LocalizationService localizationService)
    {
        Fields = [];
        this.localizationService = localizationService;

        // Subscribe to language changes to update field labels
        if (localizationService != null)
        {
            localizationService.PropertyChanged += OnLanguageChanged;
        }
    }

    public void AddField(BE.IEditableSetting setting, object inputControl)
    {
        if (setting == null || inputControl == null)
            return;

        var fieldItem = new FieldItemViewModel(setting, inputControl, localizationService);
        Fields.Add(fieldItem);
    }

    public void AddField(string localizationKey, object inputControl)
    {
        if (string.IsNullOrWhiteSpace(localizationKey) || inputControl == null)
            return;

        var fieldItem = new FieldItemViewModel(localizationKey, inputControl, localizationService);
        Fields.Add(fieldItem);
    }

    public void RemoveField(int index)
    {
        if (index >= 0 && index < Fields.Count)
        {
            Fields.RemoveAt(index);
        }
    }

    public void RemoveField(FieldItemViewModel field)
    {
        if (field != null)
        {
            Fields.Remove(field);
        }
    }

    public void ClearFields()
    {
        Fields.Clear();
    }

    private void OnLanguageChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == LocalizationService.LanguageChangedPropertyName)
        {
            // Notify all FieldItemViewModel instances to update their labels
            foreach (var field in Fields)
            {
                field.UpdateLabel();
            }
        }
    }
}

public partial class FieldItemViewModel : ViewModelBase
{
    private readonly BE.IEditableSetting? setting;
    private readonly string? localizationKey;
    private readonly LocalizationService localizationService;

    [ObservableProperty]
    private string label = string.Empty;

    public object InputControl { get; }

    // Constructor for EditableSetting
    public FieldItemViewModel(BE.IEditableSetting setting, object inputControl, LocalizationService localizationService)
    {
        this.setting = setting ?? throw new ArgumentNullException(nameof(setting));
        InputControl = inputControl ?? throw new ArgumentNullException(nameof(inputControl));
        this.localizationService = localizationService ?? throw new ArgumentNullException(nameof(localizationService));
        label = GetLocalizedLabel();
    }

    // Constructor for localization key
    public FieldItemViewModel(string localizationKey, object inputControl, LocalizationService localizationService)
    {
        this.localizationKey = localizationKey ?? throw new ArgumentNullException(nameof(localizationKey));
        InputControl = inputControl ?? throw new ArgumentNullException(nameof(inputControl));
        this.localizationService = localizationService ?? throw new ArgumentNullException(nameof(localizationService));
        label = GetLocalizedLabel();
    }

    public void UpdateLabel()
    {
        Label = GetLocalizedLabel();
    }

    private string GetLocalizedLabel()
    {
        // If we have a direct localization key, use it
        if (!string.IsNullOrEmpty(localizationKey))
        {
            return localizationService?.GetText(localizationKey) ?? localizationKey;
        }

        // If we have an EditableSetting with a localization key, use it
        if (setting != null && !string.IsNullOrEmpty(setting.LocalizationKey))
        {
            return localizationService?.GetText(setting.LocalizationKey) ?? setting.SettingLabel;
        }

        // Otherwise, use the setting label directly (for user input settings)
        return setting?.SettingLabel ?? string.Empty;
    }
}