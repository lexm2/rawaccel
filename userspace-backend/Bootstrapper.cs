using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using DATA = userspace_backend.Data;
using userspace_backend.Model;

namespace userspace_backend
{
    // TODO: remove before release
    public class Bootstrapper : IBackEndLoader
    {
        public DATA.Device[] DevicesToLoad { get; set; } = null!;

        public DATA.MappingSet MappingsToLoad { get; set; } = null!;

        public DATA.Profile[] ProfilesToLoad { get; set; } = null!;

        public DATA.Settings SettingsToLoad { get; set; } = null!;

        // Allows us to test parts of BackEndLoader as desired
        public BackEndLoader BackEndLoader { get; set; } = null!;

        public IEnumerable<DATA.Device> LoadDevices()
        {
            return DevicesToLoad;
        }

        public DATA.MappingSet LoadMappings()
        {
            return MappingsToLoad;
        }

        public IEnumerable<DATA.Profile> LoadProfiles()
        {
            return ProfilesToLoad;
        }

        public void WriteSettingsToDisk(IEnumerable<IDeviceModel> devices, MappingsModel mappings, IEnumerable<IProfileModel> profiles)
        {
            BackEndLoader.WriteSettingsToDisk(devices, mappings, profiles);
        }

        public DATA.Settings LoadSettings()
        {
            return SettingsToLoad ?? new DATA.Settings();
        }

        public void WriteSettings(DATA.Settings settings)
        {
            // Bootstrapper can delegate to BackEndLoader if needed
            BackEndLoader?.WriteSettings(settings);
        }
    }
}
