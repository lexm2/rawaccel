using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace userinterface.Services.Events;

/// <summary>
/// Thread-safe implementation of the event bus.
/// </summary>
public class EventBus : IEventBus
{
    private readonly ConcurrentDictionary<Type, List<Delegate>> _syncHandlers = new();
    private readonly ConcurrentDictionary<Type, List<Delegate>> _asyncHandlers = new();

    public IDisposable Subscribe<T>(Action<T> handler) where T : IEvent
    {
        return AddHandler(_syncHandlers, typeof(T), handler);
    }

    public IDisposable SubscribeAsync<T>(Func<T, Task> handler) where T : IEvent
    {
        return AddHandler(_asyncHandlers, typeof(T), handler);
    }

    public void Publish<T>(T @event) where T : IEvent
    {
        // Run sync handlers
        InvokeSyncHandlers(@event);

        // Fire-and-forget async handlers
        _ = InvokeAsyncHandlers(@event);
    }

    public async Task PublishAsync<T>(T @event) where T : IEvent
    {
        // Run sync handlers first
        InvokeSyncHandlers(@event);

        // Await all async handlers
        await InvokeAsyncHandlers(@event);
    }

    private void InvokeSyncHandlers<T>(T @event) where T : IEvent
    {
        if (_syncHandlers.TryGetValue(typeof(T), out var handlers))
        {
            Delegate[] snapshot;
            lock (handlers)
            {
                snapshot = handlers.ToArray();
            }

            foreach (var handler in snapshot.Cast<Action<T>>())
            {
                try
                {
                    handler(@event);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"EventBus: Error in sync handler for {typeof(T).Name}: {ex}");
                }
            }
        }
    }

    private async Task InvokeAsyncHandlers<T>(T @event) where T : IEvent
    {
        if (_asyncHandlers.TryGetValue(typeof(T), out var handlers))
        {
            Delegate[] snapshot;
            lock (handlers)
            {
                snapshot = handlers.ToArray();
            }

            var tasks = snapshot
                .Cast<Func<T, Task>>()
                .Select(async h =>
                {
                    try
                    {
                        await h(@event);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"EventBus: Error in async handler for {typeof(T).Name}: {ex}");
                    }
                });

            await Task.WhenAll(tasks);
        }
    }

    private IDisposable AddHandler(
        ConcurrentDictionary<Type, List<Delegate>> dict,
        Type type,
        Delegate handler)
    {
        var handlers = dict.GetOrAdd(type, _ => new List<Delegate>());

        lock (handlers)
        {
            handlers.Add(handler);
        }

        return new Subscription(() =>
        {
            lock (handlers)
            {
                handlers.Remove(handler);
            }
        });
    }

    private class Subscription : IDisposable
    {
        private readonly Action _unsubscribe;
        private bool _disposed;

        public Subscription(Action unsubscribe) => _unsubscribe = unsubscribe;

        public void Dispose()
        {
            if (_disposed) return;
            _unsubscribe();
            _disposed = true;
        }
    }
}
