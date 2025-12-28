using System;

namespace userinterface.Services.Events;

/// <summary>
/// Abstract base record for events. Provides automatic timestamp.
/// </summary>
public abstract record EventBase : IEvent
{
    public DateTime Timestamp { get; } = DateTime.UtcNow;
}
