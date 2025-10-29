using System;
using System.Threading;
using System.Threading.Tasks;

namespace userinterface.Utilities;

public class DebouncedAction : IDisposable
{
    private readonly int delayMilliseconds;
    private readonly Action action;
    private CancellationTokenSource? cancellationTokenSource;
    private readonly SemaphoreSlim semaphore = new(1, 1);
    private bool disposed = false;

    public DebouncedAction(Action action, int delayMilliseconds = 200)
    {
        this.action = action ?? throw new ArgumentNullException(nameof(action));
        this.delayMilliseconds = delayMilliseconds;
    }

    public void Invoke()
    {
        _ = InvokeAsync();
    }

    public async Task InvokeAsync()
    {
        if (disposed)
            return;

        await semaphore.WaitAsync();
        try
        {
            cancellationTokenSource?.Cancel();
            cancellationTokenSource?.Dispose();
            cancellationTokenSource = new CancellationTokenSource();

            var localToken = cancellationTokenSource.Token;

            _ = Task.Run(async () =>
            {
                try
                {
                    await Task.Delay(delayMilliseconds, localToken);

                    if (!localToken.IsCancellationRequested && !disposed)
                    {
                        action();
                    }
                }
                catch (TaskCanceledException)
                {
                }
            }, localToken);
        }
        finally
        {
            semaphore.Release();
        }
    }

    public void Dispose()
    {
        if (disposed)
            return;

        disposed = true;
        cancellationTokenSource?.Cancel();
        cancellationTokenSource?.Dispose();
        semaphore?.Dispose();
        GC.SuppressFinalize(this);
    }
}

public class DebouncedAction<T> : IDisposable
{
    private readonly int delayMilliseconds;
    private readonly Action<T> action;
    private CancellationTokenSource? cancellationTokenSource;
    private readonly SemaphoreSlim semaphore = new(1, 1);
    private bool disposed = false;
    private T? lastValue;

    public DebouncedAction(Action<T> action, int delayMilliseconds = 200)
    {
        this.action = action ?? throw new ArgumentNullException(nameof(action));
        this.delayMilliseconds = delayMilliseconds;
    }

    public void Invoke(T value)
    {
        _ = InvokeAsync(value);
    }

    public async Task InvokeAsync(T value)
    {
        if (disposed)
            return;

        await semaphore.WaitAsync();
        try
        {
            lastValue = value;

            cancellationTokenSource?.Cancel();
            cancellationTokenSource?.Dispose();
            cancellationTokenSource = new CancellationTokenSource();

            var localToken = cancellationTokenSource.Token;
            var localValue = lastValue;

            _ = Task.Run(async () =>
            {
                try
                {
                    await Task.Delay(delayMilliseconds, localToken);

                    if (!localToken.IsCancellationRequested && !disposed)
                    {
                        action(localValue!);
                    }
                }
                catch (TaskCanceledException)
                {
                }
            }, localToken);
        }
        finally
        {
            semaphore.Release();
        }
    }

    public void Dispose()
    {
        if (disposed)
            return;

        disposed = true;
        cancellationTokenSource?.Cancel();
        cancellationTokenSource?.Dispose();
        semaphore?.Dispose();
        GC.SuppressFinalize(this);
    }
}
