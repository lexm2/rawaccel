using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using DATA = userspace_backend.Data;
using userspace_backend.IO;
using userspace_backend.IO.PathProviders;
using userspace_backend.Model;

namespace userspace_backend
{
    public interface IBackEndLoader
    {
        public IEnumerable<DATA.Device> LoadDevices();

        public DATA.MappingSet LoadMappings();

        public IEnumerable<DATA.Profile> LoadProfiles();

        public DATA.Settings? LoadSettings();

        public void WriteSettingsToDisk(
            IEnumerable<IDeviceModel> devices,
            MappingsModel mappings,
            IEnumerable<IProfileModel> profiles);

        public void WriteSettings(DATA.Settings settings);
    }

    public class BackEndLoader : IBackEndLoader
    {
        public BackEndLoader(
            ISettingsPathProvider pathProvider,
            DevicesReaderWriter devicesReaderWriter,
            MappingsReaderWriter mappingsReaderWriter,
            ProfileReaderWriter profileReaderWriter,
            SettingsReaderWriter settingsReaderWriter)
        {
            PathProvider = pathProvider;
            DevicesReaderWriter = devicesReaderWriter;
            MappingsReaderWriter = mappingsReaderWriter;
            ProfileReaderWriter = profileReaderWriter;
            SettingsReaderWriter = settingsReaderWriter;
        }

        protected ISettingsPathProvider PathProvider { get; }
        protected DevicesReaderWriter DevicesReaderWriter { get; }
        protected MappingsReaderWriter MappingsReaderWriter { get; }
        protected ProfileReaderWriter ProfileReaderWriter { get; }
        protected SettingsReaderWriter SettingsReaderWriter { get; }

        public IEnumerable<DATA.Device> LoadDevices()
        {
            string devicesFile = PathProvider.GetDevicesFilePath();
            if (!File.Exists(devicesFile))
            {
                return [];
            }
            string devicesText = File.ReadAllText(devicesFile);
            IEnumerable<DATA.Device> devicesData = DevicesReaderWriter.Deserialize(devicesText);
            return devicesData;
        }

        public DATA.MappingSet LoadMappings()
        {
            string mappingsFile = PathProvider.GetMappingsFilePath();
            if (!File.Exists(mappingsFile))
            {
                return new DATA.MappingSet { Mappings = [] };
            }
            string mappingsText = File.ReadAllText(mappingsFile);
            DATA.MappingSet mappingsData = MappingsReaderWriter.Deserialize(mappingsText);
            return mappingsData;
        }

        public IEnumerable<DATA.Profile> LoadProfiles()
        {
            string profilesDirectory = PathProvider.GetProfilesDirectoryPath();
            if (!Directory.Exists(profilesDirectory))
            {
                return [];
            }

            string[] profileFiles = Directory.GetFiles(profilesDirectory, "*.json");
            List<DATA.Profile> profiles = [];
            foreach (string profileFile in profileFiles)
            {
                string profileText = File.ReadAllText(profileFile);
                DATA.Profile profileData = ProfileReaderWriter.Deserialize(profileText);
                profiles.Add(profileData);
            }

            return profiles;
        }

        public DATA.Settings? LoadSettings()
        {
            string settingsFile = PathProvider.GetSettingsFilePath();

            if (!File.Exists(settingsFile))
            {
                return null;
            }

            try
            {
                return SettingsReaderWriter.Read(settingsFile);
            }
            catch
            {
                return null;
            }
        }

        public void WriteSettings(DATA.Settings settings)
        {
            string settingsFile = PathProvider.GetSettingsFilePath();
            SettingsReaderWriter.Write(settingsFile, settings);
        }

        public void WriteSettingsToDisk(
            IEnumerable<IDeviceModel> devices,
            MappingsModel mappings,
            IEnumerable<IProfileModel> profiles)
        {
            WriteDevices(devices);
            WriteMappings(mappings);
            WriteProfiles(profiles);
        }

        protected void WriteDevices(IEnumerable<IDeviceModel> devices)
        {
            IEnumerable<DATA.Device> devicesData = devices.Select(d => d.MapToData());
            string devicesFileText = DevicesReaderWriter.Serialize(devicesData);
            string devicesFilePath = PathProvider.GetDevicesFilePath();
            File.WriteAllText(devicesFilePath, devicesFileText);
        }

        protected void WriteMappings(MappingsModel mappings)
        {
            DATA.MappingSet mappingsData = mappings.MapToData();
            string mappingsFileText = MappingsReaderWriter.Serialize(mappingsData);
            string mappingsFilePath = PathProvider.GetMappingsFilePath();
            File.WriteAllText(mappingsFilePath, mappingsFileText);
        }
        
        protected void WriteProfiles(IEnumerable<IProfileModel> profiles)
        {
            string profilesDirectory = PathProvider.GetProfilesDirectoryPath();
            Directory.CreateDirectory(profilesDirectory);

            foreach (var profile in profiles)
            {
                DATA.Profile profileData = profile.MapToData();
                string profileFileText = ProfileReaderWriter.Serialize(profileData);
                string profileFilePath = PathProvider.GetProfileFilePath(profileData.Name);
                File.WriteAllText(profileFilePath, profileFileText);
            }
        }
    }
}
