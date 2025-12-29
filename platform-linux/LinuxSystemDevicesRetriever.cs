using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using userspace_backend.Model;

namespace userspace_backend.Platform.Linux
{
    /// <summary>
    /// Linux implementation that retrieves mouse devices from /sys/class/input.
    /// Uses sysfs to enumerate HID devices and extract vendor/product information.
    /// </summary>
    public class LinuxSystemDevicesRetriever : ISystemDevicesRetriever
    {
        private const string InputClassPath = "/sys/class/input";
        private const ushort BusTypeUSB = 0x0003;
        private const ushort BusTypeBluetooth = 0x0005;

        public IList<ISystemDevice> GetSystemDevices()
        {
            DebugLogger.LogHeader("GET SYSTEM DEVICES (Linux sysfs)");
            var devices = new List<ISystemDevice>();

            try
            {
                if (!Directory.Exists(InputClassPath))
                {
                    Console.WriteLine($"Warning: {InputClassPath} not found");
                    DebugLogger.LogCollection("System Devices", devices);
                    return devices;
                }

                // Enumerate all mouse* directories
                var mouseDirs = Directory.GetDirectories(InputClassPath, "mouse*");
                Console.WriteLine($"Found {mouseDirs.Length} mouse device(s)");

                foreach (var mouseDir in mouseDirs)
                {
                    try
                    {
                        var device = ReadDeviceInfo(mouseDir);
                        if (device != null)
                        {
                            devices.Add(device);
                            Console.WriteLine($"  Detected: {device.Name} ({device.HWID})");
                        }
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Warning: Failed to read {mouseDir}: {ex.Message}");
                    }
                }

                // Keep all devices - don't deduplicate (wireless, wired, multiple interfaces are separate)
                DebugLogger.LogCollection("System Devices", devices);
                return devices;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error enumerating input devices: {ex.Message}");
                DebugLogger.LogCollection("System Devices", devices);
                return devices;
            }
        }

        private ISystemDevice? ReadDeviceInfo(string mouseDir)
        {
            var deviceDir = Path.Combine(mouseDir, "device");

            // Read device name
            var namePath = Path.Combine(deviceDir, "name");
            if (!File.Exists(namePath))
            {
                return null;
            }

            var name = File.ReadAllText(namePath).Trim();
            if (string.IsNullOrWhiteSpace(name))
            {
                return null;
            }

            // Read ID files
            var idDir = Path.Combine(deviceDir, "id");
            if (!Directory.Exists(idDir))
            {
                return null;
            }

            var bustypePath = Path.Combine(idDir, "bustype");
            var vendorPath = Path.Combine(idDir, "vendor");
            var productPath = Path.Combine(idDir, "product");

            if (!File.Exists(bustypePath) || !File.Exists(vendorPath) || !File.Exists(productPath))
            {
                return null;
            }

            // Parse hex values from sysfs
            ushort bustype, vendor, product;
            try
            {
                bustype = ParseHex(File.ReadAllText(bustypePath).Trim());
                vendor = ParseHex(File.ReadAllText(vendorPath).Trim());
                product = ParseHex(File.ReadAllText(productPath).Trim());
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Warning: Failed to parse device IDs in {mouseDir}: {ex.Message}");
                return null;
            }

            // Filter to HID devices only (USB or Bluetooth)
            if (bustype != BusTypeUSB && bustype != BusTypeBluetooth)
            {
                Console.WriteLine($"Skipping non-HID device: {name} (bustype: 0x{bustype:X4})");
                return null;
            }

            // Construct Windows-compatible HWID
            // Format: HID\VID_XXXX&PID_XXXX&MI_00
            var hwid = $"HID\\VID_{vendor:X4}&PID_{product:X4}&MI_00";

            // Find event device path for active device detection
            // /sys/class/input/mouse1/device → ../../input11 → /sys/class/input/input11/event10
            string? eventPath = null;
            try
            {
                var devicePath = Path.Combine(mouseDir, "device");
                if (Directory.Exists(devicePath))
                {
                    var deviceRealPath = Path.GetFullPath(devicePath);
                    var inputName = Path.GetFileName(deviceRealPath);
                    var inputPath = Path.Combine("/sys/class/input", inputName);

                    if (Directory.Exists(inputPath))
                    {
                        var eventDirs = Directory.GetDirectories(inputPath, "event*");
                        if (eventDirs.Length > 0)
                        {
                            var eventNum = Path.GetFileName(eventDirs[0]);
                            eventPath = $"/dev/input/{eventNum}";
                            Console.WriteLine($"  Mapped to event device: {eventPath}");
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Warning: Failed to map event device for {name}: {ex.Message}");
            }

            return new LinuxSystemDevice(name, hwid, eventPath);
        }

        private ushort ParseHex(string hexString)
        {
            // sysfs provides values in hex format (e.g., "046d" or "0x046d")
            // Remove "0x" prefix if present
            if (hexString.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                hexString = hexString.Substring(2);
            }

            return Convert.ToUInt16(hexString, 16);
        }
    }

    /// <summary>
    /// Linux implementation of ISystemDevice.
    /// Represents a detected HID device with name, hardware ID, and event device path.
    /// </summary>
    internal record LinuxSystemDevice(string Name, string HWID, string? EventDevicePath) : ISystemDevice;
}
