using System.Collections.Generic;
using System.Linq;
using userspace_backend.Driver.Types;
using userspace_backend.Model;

namespace userspace_backend.Driver.Debug
{
    /// <summary>
    /// Debug driver service that logs all data sent to it as JSON.
    /// Useful for debugging on both Windows and Linux.
    /// </summary>
    public class DebugDriverService : IDriverService
    {
        public bool IsAvailable => true;

        public void Activate(MappingModel mapping, IEnumerable<IDeviceModel> devices)
        {
            DebugLogger.LogHeader("ACTIVATE");

            // Log mapping info
            var mappingInfo = new
            {
                Name = mapping.Name.ModelValue,
                GroupToProfileMappings = mapping.IndividualMappings.Select(m => new
                {
                    DeviceGroup = m.DeviceGroup,
                    ProfileName = m.Profile.Name.ModelValue
                }).ToList()
            };
            DebugLogger.LogJson("Mapping", mappingInfo);

            // Log profiles using DriverMapper
            IEnumerable<DriverProfile> profiles = DriverMapper.MapProfilesFromMapping(mapping);
            DebugLogger.LogCollection("Profiles", profiles);

            // Log devices using DriverMapper
            IEnumerable<DriverDeviceSettings> deviceSettings = DriverMapper.MapDevicesFromMapping(mapping, devices);
            DebugLogger.LogCollection("Devices", deviceSettings);
        }

        public void Deactivate()
        {
            DebugLogger.LogHeader("DEACTIVATE");
            DebugLogger.Log("Driver deactivated");
        }
    }
}
