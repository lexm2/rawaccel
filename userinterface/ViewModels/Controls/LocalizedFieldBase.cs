using CommunityToolkit.Mvvm.Messaging;
using Microsoft.Extensions.DependencyInjection;
using userinterface.Services;
using userinterface.Services.Events;
using userspace_backend.Model.EditableSettings;

namespace userinterface.ViewModels.Controls;

/// <summary>
/// Base implementation of ILocalizedField with auto-localization support.
/// Uses App.Services to access services - no constructor injection needed.
/// Subscribes to LanguageChangedEvent via WeakReferenceMessenger for auto-updates.
/// </summary>
public class LocalizedFieldBase : ViewModelBase, ILocalizedField, IRecipient<LanguageChangedEvent>
{
    private readonly IEditableSetting? _setting;
    private readonly string? _localizationKey;
    private string _valueText = "";

    /// <summary>
    /// Creates a field bound to a backend setting.
    /// </summary>
    public LocalizedFieldBase(IEditableSetting setting)
    {
        _setting = setting;
        _localizationKey = setting.LocalizationKey;
        _valueText = setting.InterfaceValue;

        // Register for language changes via WeakReferenceMessenger
        WeakReferenceMessenger.Default.Register(this);
    }

    /// <summary>
    /// Creates a display-only field with a localization key (no backend setting).
    /// </summary>
    public LocalizedFieldBase(string localizationKey)
    {
        _localizationKey = localizationKey;

        // Register for language changes via WeakReferenceMessenger
        WeakReferenceMessenger.Default.Register(this);
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
                WeakReferenceMessenger.Default.Send(new FieldValueChangedEvent(this, oldValue, value));

                // Auto-apply changes to backend to trigger graph updates
                TryApply();
            }
        }
    }

    /// <summary>
    /// Push UI value to backend. Returns false if validation fails.
    /// </summary>
    public virtual bool TryApply()
    {
        if (_setting == null) return true;
        _setting.InterfaceValue = ValueText;
        var success = _setting.TryUpdateFromInterface();

        WeakReferenceMessenger.Default.Send(new FieldAppliedEvent(this, success, ValueText));

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

            WeakReferenceMessenger.Default.Send(new FieldResetEvent(this, ValueText));
        }
    }

    private string GetLocalizedLabel()
    {
        var localization = App.Services?.GetService<LocalizationService>();
        if (_localizationKey != null && localization != null)
        {
            var text = localization.GetText(_localizationKey);
            if (text != null) return text;
        }
        return _setting?.SettingLabel ?? _localizationKey ?? "";
    }

    /// <summary>
    /// Handle language changed message.
    /// </summary>
    public void Receive(LanguageChangedEvent message)
    {
        OnPropertyChanged(nameof(Label));
    }
}
