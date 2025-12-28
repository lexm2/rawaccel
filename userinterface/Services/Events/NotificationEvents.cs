using System;
using userinterface.Models;

namespace userinterface.Services.Events;

/// <summary>
/// Published when a toast notification should be shown.
/// </summary>
public record ToastRequestedEvent(
    string MessageKey,
    ToastType Type,
    TimeSpan Duration,
    object[] FormatArgs
);

/// <summary>
/// Published when the current toast should be dismissed.
/// </summary>
public record ToastDismissedEvent();
