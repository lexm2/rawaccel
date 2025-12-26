using System.Collections.Generic;
using userspace_backend;
using userspace_backend.Model;
using DATA = userspace_backend.Data;

namespace userspace_backend.Driver.Debug
{
    /// <summary>
    /// Debug backend loader that provides mock data for testing on non-Windows platforms.
    /// Returns hardcoded devices, profiles, mappings, and settings instead of reading from files.
    /// </summary>
    public class DebugBackEndLoader : IBackEndLoader
    {
        public IEnumerable<DATA.Device> LoadDevices()
        {
            DebugLogger.LogHeader("LOAD DEVICES");
            var devices = new[]
            {
                new DATA.Device() { Name = "Superlight 2", DPI = 32000, HWID = @"HID\VID_046D&PID_C54D&MI_00", PollingRate = 1000, DeviceGroup = "Logitech Mice" },
                new DATA.Device() { Name = "Outset AX", DPI = 1200, HWID = @"HID\VID_3057&PID_0001", PollingRate = 1000, DeviceGroup = "Testing" },
                new DATA.Device() { Name = "Razer Viper 8K", DPI = 1200, HWID = @"HID\VID_31E3&PID_1310", PollingRate = 1000, DeviceGroup = "Testing" },
            };
            DebugLogger.LogCollection("Devices", devices);
            return devices;
        }

        public IEnumerable<DATA.Profile> LoadProfiles()
        {
            DebugLogger.LogHeader("LOAD PROFILES");
            DebugLogger.Log("Returning empty profiles - backend will create defaults");
            return [];  // Empty - let BackEnd.EnsureDefaultProfileExists() create defaults
        }

        public DATA.MappingSet LoadMappings()
        {
            DebugLogger.LogHeader("LOAD MAPPINGS");
            DebugLogger.Log("Returning empty mappings - backend will create defaults");
            return new DATA.MappingSet { Mappings = [] };  // Empty - let BackEnd.EnsureDefaultMappingExists() create defaults
        }

        public DATA.Settings? LoadSettings()
        {
            DebugLogger.LogHeader("LOAD SETTINGS");
            var settings = new DATA.Settings()
            {
                ShowToastNotifications = true,
                ShowConfirmModals = true,
                Theme = "Dark",
                Language = "en-US"
            };
            DebugLogger.LogJson("Settings", settings);
            return settings;
        }

        public void WriteSettingsToDisk(
            IEnumerable<IDeviceModel> devices,
            MappingsModel mappings,
            IEnumerable<IProfileModel> profiles)
        {
            DebugLogger.LogHeader("WRITE SETTINGS TO DISK");
            DebugLogger.Log("WriteSettingsToDisk called (no-op in debug mode)");
        }

        public void WriteSettings(DATA.Settings settings)
        {
            DebugLogger.LogHeader("WRITE SETTINGS");
            DebugLogger.LogJson("Settings to write", settings);
            DebugLogger.Log("WriteSettings called (no-op in debug mode)");
        }
    }
}
