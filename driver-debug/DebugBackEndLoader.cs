using System.Collections.Generic;
using userspace_backend;
using userspace_backend.Data.Profiles;
using userspace_backend.Data.Profiles.Accel;
using userspace_backend.Data.Profiles.Accel.Formula;
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
            var profiles = new[]
            {
                new DATA.Profile
                {
                    Name = "Default",
                    OutputDPI = 1600,
                    YXRatio = 1.0,
                    Acceleration = new NoAcceleration(),
                    Hidden = new Hidden
                    {
                        LeftRightRatio = 1.0,
                        UpDownRatio = 1.0,
                        RotationDegrees = 0.0,
                        AngleSnappingDegrees = 0.0,
                        SpeedCap = 0.0,
                        OutputSmoothingHalfLife = 0.0
                    }
                },
                new DATA.Profile
                {
                    Name = "Classic Accel",
                    OutputDPI = 1600,
                    YXRatio = 1.0,
                    Acceleration = new ClassicAccel
                    {
                        Gain = false,
                        Acceleration = 0.05,
                        Exponent = 2.0,
                        Offset = 0.0,
                        Cap = 0.0
                    },
                    Hidden = new Hidden
                    {
                        LeftRightRatio = 1.0,
                        UpDownRatio = 1.0,
                        RotationDegrees = 0.0,
                        AngleSnappingDegrees = 0.0,
                        SpeedCap = 0.0,
                        OutputSmoothingHalfLife = 0.0
                    }
                },
                new DATA.Profile
                {
                    Name = "Natural Accel",
                    OutputDPI = 1600,
                    YXRatio = 1.0,
                    Acceleration = new NaturalAccel
                    {
                        Gain = false,
                        DecayRate = 0.5,
                        InputOffset = 0.0,
                        Limit = 3.0
                    },
                    Hidden = new Hidden
                    {
                        LeftRightRatio = 1.0,
                        UpDownRatio = 1.0,
                        RotationDegrees = 0.0,
                        AngleSnappingDegrees = 0.0,
                        SpeedCap = 0.0,
                        OutputSmoothingHalfLife = 0.0
                    }
                }
            };
            DebugLogger.LogCollection("Profiles", profiles);
            return profiles;
        }

        public DATA.MappingSet LoadMappings()
        {
            DebugLogger.LogHeader("LOAD MAPPINGS");
            var mappingSet = new DATA.MappingSet
            {
                ActiveMappingIndex = 0,
                Mappings =
                [
                    new DATA.Mapping
                    {
                        Name = "Default Mapping",
                        GroupsToProfiles = new DATA.Mapping.GroupsToProfilesMapping
                        {
                            { "Logitech Mice", "Classic Accel" },
                            { "Testing", "Default" }
                        }
                    },
                    new DATA.Mapping
                    {
                        Name = "All Natural",
                        GroupsToProfiles = new DATA.Mapping.GroupsToProfilesMapping
                        {
                            { "Logitech Mice", "Natural Accel" },
                            { "Testing", "Natural Accel" }
                        }
                    }
                ]
            };
            DebugLogger.LogJson("Mappings", mappingSet);
            return mappingSet;
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
