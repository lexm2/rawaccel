using System;
using System.Collections.Generic;
using System.Linq;
using DATA = userspace_backend.Data;
using userspace_backend.Model;
using Microsoft.Extensions.DependencyInjection;

namespace userspace_backend
{
    public interface IBackEnd
    {
        void Load();

        void Apply();

        DevicesModel Devices { get; }

        MappingsModel Mappings { get; }

        IProfilesModel Profiles { get; }
    }

    public class BackEnd : IBackEnd
    {
        public BackEnd(
            IBackEndLoader backEndLoader,
            IProfilesModel profilesModel,
            DevicesModel devicesModel,
            MappingsModel mappingsModel,
            IServiceProvider serviceProvider)
        {
            BackEndLoader = backEndLoader;
            Devices = devicesModel;
            Mappings = mappingsModel;
            Profiles = profilesModel;
            ServiceProvider = serviceProvider;
        }

        public DevicesModel Devices { get; set; }

        public MappingsModel Mappings { get; set; }

        public IProfilesModel Profiles { get; set; }

        protected IBackEndLoader BackEndLoader { get; set; }

        protected IServiceProvider ServiceProvider { get; set; }

        public void Load()
        {
            IEnumerable<DATA.Device> devicesData = BackEndLoader.LoadDevices();
            LoadDevicesFromData(devicesData);

            IEnumerable<DATA.Profile> profilesData = BackEndLoader.LoadProfiles();
            LoadProfilesFromData(profilesData);

            DATA.MappingSet mappingData = BackEndLoader.LoadMappings();
            LoadMappingsFromData(mappingData);

            EnsureDefaultDeviceGroupExists();
            EnsureDefaultDeviceExists();
            EnsureDefaultProfileExists();
            EnsureDefaultMappingExists();
        }

        protected void LoadDevicesFromData(IEnumerable<DATA.Device> devicesData)
        {
            Devices.TryMapFromData(devicesData);
        }

        protected void LoadProfilesFromData(IEnumerable<DATA.Profile> profileData)
        {
            Profiles.TryMapFromData(profileData);
        }

        protected void LoadMappingsFromData(DATA.MappingSet mappingData)
        {
            // Clear existing mappings and reload from data
            Mappings.Mappings.Clear();
            foreach (var mapping in mappingData.Mappings)
            {
                Mappings.TryAddMapping(mapping);
            }
        }

        protected void EnsureDefaultDeviceGroupExists()
        {
            // If no device groups exist, create a "Default" group
            if (Devices.DeviceGroups.DeviceGroupModels.Count == 0)
            {
                Devices.DeviceGroups.AddOrGetDeviceGroup(DeviceGroups.DefaultDeviceGroup);
            }
        }

        protected void EnsureDefaultDeviceExists()
        {
            // If no devices exist, create a default device
            if (Devices.Elements.Count == 0)
            {
                var defaultDevice = ServiceProvider.GetRequiredService<IDeviceModel>();
                defaultDevice.Name.TryUpdateModelDirectly("Default");
                defaultDevice.HardwareID.TryUpdateModelDirectly("DEFAULT_DEVICE_ID");
                defaultDevice.DeviceGroup.TryUpdateModelDirectly(DeviceGroups.DefaultDeviceGroup);
                // DPI, PollRate, and Ignore already have sensible defaults from DI (1000, 1000, false)

                Devices.TryInsert(0, defaultDevice);
            }
        }

        protected void EnsureDefaultProfileExists()
        {
            // If no profiles exist, create a default profile
            if (Profiles.Elements.Count == 0)
            {
                var defaultProfile = ServiceProvider.GetRequiredService<IProfileModel>();
                defaultProfile.Name.TryUpdateModelDirectly("Default");
                Profiles.TryInsert(0, defaultProfile);
            }
        }

        protected void EnsureDefaultMappingExists()
        {
            // If no mappings exist, create a default mapping
            if (Mappings.Mappings.Count == 0)
            {
                var defaultMapping = new DATA.Mapping
                {
                    Name = "Default",
                    GroupsToProfiles = new DATA.Mapping.GroupsToProfilesMapping
                    {
                        { DeviceGroups.DefaultDeviceGroup, "Default" }
                    }
                };

                if (Mappings.TryAddMapping(defaultMapping))
                {
                    // Set this as the active mapping
                    if (Mappings.TryGetMapping("Default", out MappingModel? mapping) && mapping != null)
                    {
                        mapping.SetActive = true;
                    }
                }
            }

            // Ensure at least one mapping has SetActive = true
            if (Mappings.GetMappingToSetActive() == null && Mappings.Mappings.Count > 0)
            {
                Mappings.Mappings[0].SetActive = true;
            }
        }

        public void Apply()
        {
            try
            {
                //WriteToDriver();
            }
            catch (Exception ex)
            {
                return;
            }

            WriteSettingsToDisk();
        }

        protected void WriteSettingsToDisk()
        {
            BackEndLoader.WriteSettingsToDisk(
                Devices.Elements,
                Mappings,
                Profiles.Elements);
        }

        protected void WriteToDriver()
        {
            MappingModel mappingToApply = Mappings.GetMappingToSetActive();
            DriverConfig config = MapToDriverConfig(mappingToApply);
            try
            {
                config.Activate();
            }
            catch(Exception ex)
            {
                // Log this once logging is added
            }
        }

        protected DriverConfig MapToDriverConfig(MappingModel mappingModel)
        {
            IEnumerable<DeviceSettings> configDevices = MapToDriverDevices(mappingModel);
            IEnumerable<Profile> configProfiles = MapToDriverProfiles(mappingModel);

            DriverConfig config = DriverConfig.GetDefault();
            config.profiles = configProfiles.ToList();
            config.devices = configDevices.ToList();
            config.accels = configProfiles.Select(p => new ManagedAccel(p)).ToList();
            return config;
        }

        protected IEnumerable<DeviceSettings> MapToDriverDevices(MappingModel mapping)
        {
            return mapping.IndividualMappings.SelectMany(
                dg => MapToDriverDevices(dg.DeviceGroup, dg.Profile.Name.ModelValue));
        }

        protected IEnumerable<Profile> MapToDriverProfiles(MappingModel mapping)
        {
            IEnumerable<IProfileModel> ProfilesToMap = mapping.IndividualMappings.Select(m => m.Profile).Distinct();
            return ProfilesToMap.Select(p => p.CurrentValidatedDriverProfile);
        }

        protected IEnumerable<DeviceSettings> MapToDriverDevices(string dg, string profileName)
        {
            IEnumerable<IDeviceModel> deviceModels = Devices.Elements.Where(d => d.DeviceGroup.ModelValue.Equals(dg));
            return deviceModels.Select(dm => MapToDriverDevice(dm, profileName));
        }

        protected DeviceSettings MapToDriverDevice(IDeviceModel deviceModel, string profileName)
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
                    pollingRate = deviceModel.DPI.ModelValue,
                    pollTimeLock = false,
                    setExtraInfo = false,
                    maximumTime = 200,
                    minimumTime = 0.1,
                }
            };
        }
    }
}
