using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using DATA = userspace_backend.Data;
using userspace_backend.IO;
using userspace_backend.Model;

namespace userspace_backend
{
    public interface IBackEndLoader
    {
        public IEnumerable<DATA.Device> LoadDevices();

        public DATA.MappingSet LoadMappings();

        public IEnumerable<DATA.Profile> LoadProfiles();

        public void WriteSettingsToDisk(
            IEnumerable<IDeviceModel> devices,
            MappingsModel mappings,
            IEnumerable<IProfileModel> profiles);
    }

    public class BackEndLoader : IBackEndLoader
    {
        public BackEndLoader(
            string settingsDirectory,
            DevicesReaderWriter devicesReaderWriter,
            MappingsReaderWriter mappingsReaderWriter,
            ProfileReaderWriter profileReaderWriter)
        {
            SettingsDirectory = settingsDirectory;
            DevicesReaderWriter = devicesReaderWriter;
            MappingsReaderWriter = mappingsReaderWriter;
            ProfileReaderWriter = profileReaderWriter;
        }

        public string SettingsDirectory { get; private set; }
        protected DevicesReaderWriter DevicesReaderWriter { get; }
        protected MappingsReaderWriter MappingsReaderWriter { get; }
        protected ProfileReaderWriter ProfileReaderWriter { get; }

        public IEnumerable<DATA.Device> LoadDevices()
        {
            string devicesFile = GetDevicesFile(SettingsDirectory);
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
            string mappingsFile = GetMappingsFile(SettingsDirectory);
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
            string profilesDirectory = GetProfilesDirectory(SettingsDirectory);
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
            string devicesFilePath = GetDevicesFile(SettingsDirectory);
            File.WriteAllText(devicesFilePath, devicesFileText);
        }

        protected void WriteMappings(MappingsModel mappings)
        {
            DATA.MappingSet mappingsData = mappings.MapToData();
            string mappingsFileText = MappingsReaderWriter.Serialize(mappingsData);
            string mappingsFilePath = GetMappingsFile(SettingsDirectory);
            File.WriteAllText(mappingsFilePath, mappingsFileText);
        }
        
        protected void WriteProfiles(IEnumerable<IProfileModel> profiles)
        {
            string profilesDirectory = GetProfilesDirectory(SettingsDirectory);
            Directory.CreateDirectory(profilesDirectory);

            foreach (var profile in profiles)
            {
                DATA.Profile profileData = profile.MapToData();
                string profileFileText = ProfileReaderWriter.Serialize(profileData);
                string profileFilePath = GetProfileFile(profilesDirectory, profileData.Name);
                File.WriteAllText(profileFilePath, profileFileText);
            }
        }

        protected static string GetDevicesFile(string settingsDirectory) => Path.Combine(settingsDirectory, "devices.json");

        protected static string GetMappingsFile(string settingsDirectory) => Path.Combine(settingsDirectory, "mappings.json");

        protected static string GetProfilesDirectory(string settingsDirectory) => Path.Combine(settingsDirectory, "profiles");

        protected static string GetProfileFile(string profileDirectory, string profileName) => Path.Combine(profileDirectory, $"{profileName}.json");
    }
}
