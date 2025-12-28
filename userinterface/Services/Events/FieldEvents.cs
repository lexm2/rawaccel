using userinterface.ViewModels.Controls;

namespace userinterface.Services.Events;

/// <summary>
/// Published when a field's ValueText property changes.
/// </summary>
public record FieldValueChangedEvent(
    ILocalizedField Field,
    string OldValue,
    string NewValue
);

/// <summary>
/// Published when TryApply() is called on a field.
/// </summary>
public record FieldAppliedEvent(
    ILocalizedField Field,
    bool Success,
    string Value
);

/// <summary>
/// Published when Reset() is called on a field.
/// </summary>
public record FieldResetEvent(
    ILocalizedField Field,
    string Value
);
