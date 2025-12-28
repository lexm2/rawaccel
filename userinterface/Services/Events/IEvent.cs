using System;

namespace userinterface.Services.Events;

/// <summary>
/// Base interface for all events in the event bus.
/// </summary>
public interface IEvent
{
    /// <summary>
    /// When the event was created.
    /// </summary>
    DateTime Timestamp { get; }
}
