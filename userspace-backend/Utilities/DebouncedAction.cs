using System;
using System.Threading;
using System.Threading.Tasks;

namespace userspace_backend.Utilities;

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
