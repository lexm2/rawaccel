using System;
using System.Timers;
using userspace_backend.Driver;

namespace userinterface.Services;

/// <summary>
/// Service that monitors mouse input speed in real-time by polling the driver.
/// Publishes events with current mouse speed data.
/// </summary>
public class MouseInputMonitorService : IDisposable
{
    private readonly IDriverService driverService;
    private Timer? pollingTimer;
    private bool isMonitoring;

    public event EventHandler<double>? MouseSpeedUpdated;

    public MouseInputMonitorService(IDriverService driverService)
    {
        this.driverService = driverService ?? throw new ArgumentNullException(nameof(driverService));
    }

    /// <summary>
    /// Starts monitoring mouse input by polling the driver every 50ms.
    /// </summary>
    public void StartMonitoring()
    {
        if (isMonitoring) return;

        Console.WriteLine("[MouseInputMonitor] Starting monitoring...");

        // Test connection first
        try
        {
            var testSpeed = driverService.GetCurrentMouseSpeed();
            Console.WriteLine($"[MouseInputMonitor] Connection test: speed = {testSpeed}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[MouseInputMonitor] Connection test failed: {ex.Message}");
        }

        pollingTimer = new Timer(50); // Poll every 50ms (20Hz)
        pollingTimer.Elapsed += OnPollTick;
        pollingTimer.AutoReset = true;
        pollingTimer.Start();

        isMonitoring = true;
        Console.WriteLine("[MouseInputMonitor] Started monitoring");
    }

    /// <summary>
    /// Stops monitoring mouse input.
    /// </summary>
    public void StopMonitoring()
    {
        if (!isMonitoring) return;

        pollingTimer?.Stop();
        pollingTimer?.Dispose();
        pollingTimer = null;

        isMonitoring = false;
        Console.WriteLine("[MouseInputMonitor] Stopped monitoring");
    }

    private void OnPollTick(object? sender, ElapsedEventArgs e)
    {
        try
        {
            var currentSpeed = driverService.GetCurrentMouseSpeed();

            // Only fire event if speed is non-zero (mouse is moving)
            if (currentSpeed > 0.01)
            {
                Console.WriteLine($"[MouseInputMonitor] Speed > threshold, firing event: {currentSpeed:F2}");
                MouseSpeedUpdated?.Invoke(this, currentSpeed);
            }
            else if (currentSpeed > 0)
            {
                Console.WriteLine($"[MouseInputMonitor] Speed below threshold (0.01), not firing event: {currentSpeed:F6}");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[MouseInputMonitor] Error polling: {ex.Message}");
        }
    }

    public void Dispose()
    {
        StopMonitoring();
    }
}
