using System.ComponentModel;

namespace userinterface.ViewModels.Controls;

/// <summary>
/// Interface for a field control with auto-localization support.
/// Each use site decides its own layout and input type.
/// </summary>
public interface ILocalizedField : INotifyPropertyChanged
{
    /// <summary>
    /// The localized label text. Auto-updates when language changes.
    /// </summary>
    string Label { get; }

    /// <summary>
    /// The current value as a string (for UI binding).
    /// Use XAML converters for non-string types (e.g., StringToBool).
    /// </summary>
    string ValueText { get; set; }

    /// <summary>
    /// Push UI value to backend. Returns false if validation fails.
    /// </summary>
    bool TryApply();

    /// <summary>
    /// Pull backend value to UI.
    /// </summary>
    void Reset();
}
