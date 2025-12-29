using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using userspace_backend.ActiveDetection;
using userspace_backend.Model;

namespace userspace_backend.Platform.Linux
{
    /// <summary>
    /// Linux implementation of active device detection.
    /// Monitors /dev/input/event* devices for input events to detect which device is currently being used.
    /// </summary>
    public class LinuxActiveDeviceDetector : IActiveDeviceDetector
    {
        private const ushort EV_REL = 0x02;  // Relative movement event (mouse)
        private const int EventSize = 24;     // sizeof(input_event) on 64-bit Linux

        public async Task<ISystemDevice?> DetectActiveDeviceAsync(
            IEnumerable<ISystemDevice> devices,
            TimeSpan timeout)
        {
            DebugLogger.LogHeader("DETECT ACTIVE DEVICE");

            var devicesList = devices.ToList();
            if (devicesList.Count == 0)
            {
                Console.WriteLine("No devices to monitor");
                return null;
            }

            // Filter devices that have event paths
            var devicesWithPaths = devicesList
                .Where(d => !string.IsNullOrEmpty(d.EventDevicePath))
                .ToList();

            if (devicesWithPaths.Count == 0)
            {
                Console.WriteLine("No devices have event paths");
                return null;
            }

            Console.WriteLine($"Monitoring {devicesWithPaths.Count} device(s) for input...");
            foreach (var device in devicesWithPaths)
            {
                Console.WriteLine($"  - {device.Name} ({device.EventDevicePath})");
            }

            using var cts = new CancellationTokenSource(timeout);

            try
            {
                // Create monitoring tasks for each device
                var tasks = devicesWithPaths.Select(device =>
                    MonitorDeviceAsync(device, cts.Token)).ToArray();

                // Wait for first device to generate input
                var completedTask = await Task.WhenAny(tasks);
                var activeDevice = await completedTask;

                if (activeDevice != null)
                {
                    Console.WriteLine($"Active device detected: {activeDevice.Name}");
                    cts.Cancel(); // Cancel other monitoring tasks
                }

                return activeDevice;
            }
            catch (OperationCanceledException)
            {
                Console.WriteLine("Detection timed out");
                return null;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error during detection: {ex.Message}");
                return null;
            }
        }

        private async Task<ISystemDevice?> MonitorDeviceAsync(ISystemDevice device, CancellationToken ct)
        {
            return await Task.Run(() =>
            {
                try
                {
                    // Open event device for reading
                    using var stream = new FileStream(
                        device.EventDevicePath!,
                        FileMode.Open,
                        FileAccess.Read,
                        FileShare.ReadWrite);

                    var buffer = new byte[EventSize];

                    while (!ct.IsCancellationRequested)
                    {
                        // Read one input_event structure
                        int bytesRead = 0;
                        while (bytesRead < EventSize && !ct.IsCancellationRequested)
                        {
                            int read = stream.Read(buffer, bytesRead, EventSize - bytesRead);
                            if (read == 0)
                                break;
                            bytesRead += read;
                        }

                        if (bytesRead < EventSize)
                            continue;

                        // Parse event structure
                        var inputEvent = ParseInputEvent(buffer);

                        // Check for relative movement (mouse movement)
                        if (inputEvent.Type == EV_REL)
                        {
                            Console.WriteLine($"Movement detected on: {device.Name}");
                            return device;
                        }
                    }

                    return null;
                }
                catch (UnauthorizedAccessException)
                {
                    Console.WriteLine($"Permission denied: {device.EventDevicePath}");
                    Console.WriteLine($"Run: sudo usermod -aG input $USER (requires logout/login)");
                    return null;
                }
                catch (FileNotFoundException)
                {
                    Console.WriteLine($"Event device not found: {device.EventDevicePath}");
                    return null;
                }
                catch (OperationCanceledException)
                {
                    return null;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error monitoring {device.Name}: {ex.Message}");
                    return null;
                }
            }, ct);
        }

        private InputEvent ParseInputEvent(byte[] buffer)
        {
            // input_event structure (64-bit Linux):
            // struct timeval time;  // 16 bytes (8 + 8)
            // __u16 type;          // 2 bytes
            // __u16 code;          // 2 bytes
            // __s32 value;         // 4 bytes
            // Total: 24 bytes

            return new InputEvent
            {
                TimeSec = BitConverter.ToInt64(buffer, 0),
                TimeUSec = BitConverter.ToInt64(buffer, 8),
                Type = BitConverter.ToUInt16(buffer, 16),
                Code = BitConverter.ToUInt16(buffer, 18),
                Value = BitConverter.ToInt32(buffer, 20)
            };
        }

        private struct InputEvent
        {
            public long TimeSec;
            public long TimeUSec;
            public ushort Type;
            public ushort Code;
            public int Value;
        }
    }
}
