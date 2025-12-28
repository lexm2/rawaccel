using System.Collections.Generic;
using userspace_backend.Model;

namespace userspace_backend.Platform.Linux
{
    /// <summary>
    /// Debug system devices retriever for non-Windows platforms.
    /// Returns mock devices that match the test data in DebugBackEndLoader.
    /// </summary>
    public class DebugSystemDevicesRetriever : ISystemDevicesRetriever
    {
        public IList<ISystemDevice> GetSystemDevices()
        {
            DebugLogger.LogHeader("GET SYSTEM DEVICES");
            var devices = new List<ISystemDevice>
            {
                new DebugSystemDevice("Superlight 2", @"HID\VID_046D&PID_C54D&MI_00"),
                new DebugSystemDevice("Outset AX", @"HID\VID_3057&PID_0001"),
                new DebugSystemDevice("Razer Viper 8K", @"HID\VID_31E3&PID_1310"),
            };
            DebugLogger.LogCollection("System Devices", devices);
            return devices;
        }
    }

    /// <summary>
    /// Simple record implementing ISystemDevice for debug/mock purposes.
    /// </summary>
    internal record DebugSystemDevice(string Name, string HWID) : ISystemDevice;
}
