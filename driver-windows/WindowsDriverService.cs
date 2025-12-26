using System;
using System.Collections.Generic;
using System.Linq;
using userspace_backend.Driver.Types;
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
            // Use shared DriverMapper to get shared types
            IEnumerable<DriverProfile> sharedProfiles = DriverMapper.MapProfilesFromMapping(mappingModel);
            IEnumerable<DriverDeviceSettings> sharedDevices = DriverMapper.MapDevicesFromMapping(mappingModel, allDevices);

            // Convert to wrapper types
            List<Profile> wrapperProfiles = sharedProfiles
                .Select(WrapperTypeConverter.ToWrapperProfile)
                .ToList();
            List<DeviceSettings> wrapperDevices = sharedDevices
                .Select(WrapperTypeConverter.ToWrapperDeviceSettings)
                .ToList();

            // Create DriverConfig with wrapper types
            DriverConfig config = DriverConfig.GetDefault();
            config.profiles = wrapperProfiles;
            config.devices = wrapperDevices;
            config.accels = wrapperProfiles.Select(p => new ManagedAccel(p)).ToList();
            return config;
        }
    }
}
