using System;
using System.ComponentModel;
using userinterface.Services;
using userinterface.Services.Events;
using userspace_backend.Model.EditableSettings;

namespace userinterface.ViewModels.Controls;

/// <summary>
/// Base implementation of ILocalizedField with auto-localization support.
/// Each use site decides its own layout and input type.
/// Publishes events through the event bus when values change.
/// </summary>
public class LocalizedFieldBase : ViewModelBase, ILocalizedField, IDisposable
{
    private readonly LocalizationService _localization;
    private readonly IEventBus? _eventBus;
    private readonly IEditableSetting? _setting;
    private readonly string? _localizationKey;
    private string _valueText = "";
    private bool _disposed;

    /// <summary>
    /// Creates a field bound to a backend setting.
    /// </summary>
    public LocalizedFieldBase(
        IEditableSetting setting,
        LocalizationService localization,
        IEventBus? eventBus = null)
    {
        _setting = setting;
        _localization = localization;
        _eventBus = eventBus;
        _localizationKey = setting.LocalizationKey;
        _valueText = setting.InterfaceValue;

        _localization.PropertyChanged += OnLanguageChanged;
    }

    /// <summary>
    /// Creates a display-only field with a localization key (no backend setting).
    /// </summary>
    public LocalizedFieldBase(
        string localizationKey,
        LocalizationService localization,
        IEventBus? eventBus = null)
    {
        _localizationKey = localizationKey;
        _localization = localization;
        _eventBus = eventBus;

        _localization.PropertyChanged += OnLanguageChanged;
    }

    /// <summary>
    /// The localized label text. Auto-updates when language changes.
    /// </summary>
    public string Label => GetLocalizedLabel();

    /// <summary>
    /// The current value as a string (for UI binding).
    /// </summary>
    public string ValueText
    {
        get => _valueText;
        set
        {
            var oldValue = _valueText;
            if (SetProperty(ref _valueText, value))
            {
                _eventBus?.Publish(new FieldValueChangedEvent(this, oldValue, value));
            }
        }
    }

    /// <summary>
    /// Push UI value to backend. Returns false if validation fails.
    /// </summary>
    public virtual bool TryApply()
    {
        if (_setting == null) return true;
        var success = _setting.TryUpdateFromInterface(ValueText);
        _eventBus?.Publish(new FieldAppliedEvent(this, success, ValueText));
        return success;
    }

    /// <summary>
    /// Pull backend value to UI.
    /// </summary>
    public virtual void Reset()
    {
        if (_setting != null)
        {
            ValueText = _setting.InterfaceValue;
            _eventBus?.Publish(new FieldResetEvent(this, ValueText));
        }
    }

    private string GetLocalizedLabel()
    {
        if (_localizationKey != null)
        {
            var text = _localization.GetText(_localizationKey);
            if (text != null) return text;
        }
        return _setting?.SettingLabel ?? _localizationKey ?? "";
    }

    private void OnLanguageChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == LocalizationService.LanguageChangedPropertyName)
            OnPropertyChanged(nameof(Label));
    }

    public void Dispose()
    {
        if (_disposed) return;
        _localization.PropertyChanged -= OnLanguageChanged;
        _disposed = true;
    }
}
