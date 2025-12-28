using System;
using System.Threading.Tasks;

namespace userinterface.Services.Events;

/// <summary>
/// Centralized event bus for type-based pub/sub communication.
/// </summary>
public interface IEventBus
{
    /// <summary>
    /// Subscribe to events of type T with a synchronous handler.
    /// </summary>
    /// <returns>Disposable subscription that unsubscribes when disposed.</returns>
    IDisposable Subscribe<T>(Action<T> handler) where T : IEvent;

    /// <summary>
    /// Subscribe to events of type T with an async handler.
    /// </summary>
    /// <returns>Disposable subscription that unsubscribes when disposed.</returns>
    IDisposable SubscribeAsync<T>(Func<T, Task> handler) where T : IEvent;

    /// <summary>
    /// Publish an event to all subscribers.
    /// Sync handlers run immediately, async handlers are fire-and-forget.
    /// </summary>
    void Publish<T>(T @event) where T : IEvent;

    /// <summary>
    /// Publish an event and await all async handlers.
    /// </summary>
    Task PublishAsync<T>(T @event) where T : IEvent;
}
