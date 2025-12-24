#if WINDOWS
using System;
using System.Collections.Generic;
using System.Linq;
using userspace_backend.Common;
using userspace_backend.Model;

namespace userspace_backend.Driver.Windows
{
    /// <summary>
    /// Windows implementation that communicates with the kernel driver.
    /// </summary>
    public class WindowsDriverService : IDriverService
    {
        public bool IsAvailable => true;

        public void Activate(MappingModel mapping, IEnumerable<IDeviceModel> devices)
        {
            DriverConfig config = MapToDriverConfig(mapping, devices);
            try
            {
                config.Activate();
            }
            catch (Exception)
            {
                // Log this once logging is added
            }
        }

        public void Deactivate()
        {
            try
            {
                DriverConfig.Deactivate();
            }
            catch (Exception)
            {
                // Log this once logging is added
            }
        }

        private DriverConfig MapToDriverConfig(MappingModel mappingModel, IEnumerable<IDeviceModel> allDevices)
        {
            IEnumerable<DeviceSettings> configDevices = MapToDriverDevices(mappingModel, allDevices);
            IEnumerable<Profile> configProfiles = MapToDriverProfiles(mappingModel);

            DriverConfig config = DriverConfig.GetDefault();
            config.profiles = configProfiles.ToList();
            config.devices = configDevices.ToList();
            config.accels = configProfiles.Select(p => new ManagedAccel(p)).ToList();
            return config;
        }

        private IEnumerable<DeviceSettings> MapToDriverDevices(MappingModel mapping, IEnumerable<IDeviceModel> allDevices)
        {
            return mapping.IndividualMappings.SelectMany(
                dg => MapToDriverDevices(dg.DeviceGroup, dg.Profile.Name.ModelValue, allDevices));
        }

        private IEnumerable<Profile> MapToDriverProfiles(MappingModel mapping)
        {
            IEnumerable<IProfileModel> profilesToMap = mapping.IndividualMappings.Select(m => m.Profile).Distinct();
            return profilesToMap
                .OfType<ProfileModel>()
                .Select(p => DriverHelpers.MapProfileModelToDriver(p));
        }

        private IEnumerable<DeviceSettings> MapToDriverDevices(string deviceGroup, string profileName, IEnumerable<IDeviceModel> allDevices)
        {
            IEnumerable<IDeviceModel> deviceModels = allDevices.Where(d => d.DeviceGroup.ModelValue.Equals(deviceGroup));
            return deviceModels.Select(dm => MapToDriverDevice(dm, profileName));
        }

        private DeviceSettings MapToDriverDevice(IDeviceModel deviceModel, string profileName)
        {
            return new DeviceSettings()
            {
                id = deviceModel.HardwareID.ModelValue,
                name = deviceModel.Name.ModelValue,
                profile = profileName,
                config = new DeviceConfig()
                {
                    disable = deviceModel.Ignore.ModelValue,
                    dpi = deviceModel.DPI.ModelValue,
                    pollingRate = deviceModel.PollRate.ModelValue,
                    pollTimeLock = false,
                    setExtraInfo = false,
                    maximumTime = 200,
                    minimumTime = 0.1,
                }
            };
        }
    }
}
#endif
