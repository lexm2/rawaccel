using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Data.Profiles;
using userspace_backend.Driver;
using userspace_backend.IO;
using userspace_backend.Model;
using DATA = userspace_backend.Data;

namespace userspace_backend
{
    public interface IBackEnd
    {
        void Load();

        bool Apply();

        DevicesModel Devices { get; }

        MappingsModel Mappings { get; }

        IProfilesModel Profiles { get; }

        DATA.Settings Settings { get; }
    }

    public class BackEnd : IBackEnd
    {
        public BackEnd(
            IBackEndLoader backEndLoader,
            IProfilesModel profilesModel,
            DevicesModel devicesModel,
            MappingsModel mappingsModel,
            IDriverService driverService,
            IServiceProvider serviceProvider)
        {
            BackEndLoader = backEndLoader;
            Devices = devicesModel;
            Mappings = mappingsModel;
            Profiles = profilesModel;
            DriverService = driverService;
            ServiceProvider = serviceProvider;
        }

        public DevicesModel Devices { get; set; }

        public MappingsModel Mappings { get; set; } = null!;

        public IProfilesModel Profiles { get; set; }

        public DATA.Settings Settings { get; set; }

        protected IBackEndLoader BackEndLoader { get; set; }

        protected IDriverService DriverService { get; set; }

        protected IServiceProvider ServiceProvider { get; set; }

        public void Load()
        {
            IEnumerable<DATA.Device> devicesData = BackEndLoader.LoadDevices();
            LoadDevicesFromData(devicesData);

            IEnumerable<DATA.Profile> profilesData = BackEndLoader.LoadProfiles();
            LoadProfilesFromData(profilesData);

            DATA.MappingSet mappingData = BackEndLoader.LoadMappings();
            LoadMappingsFromData(mappingData);

            Settings = BackEndLoader.LoadSettings() ?? new DATA.Settings();

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

        public bool Apply()
        {
            try
            {
                WriteToDriver();
            }
            catch (Exception)
            {
                return false;
            }

            WriteSettingsToDisk();
            return true;
        }

        protected void WriteSettingsToDisk()
        {
            BackEndLoader.WriteSettingsToDisk(
                Devices.Elements,
                Mappings,
                Profiles.Elements);

            BackEndLoader.WriteSettings(Settings);
        }

        protected void WriteToDriver()
        {
            MappingModel? mappingToApply = Mappings.GetMappingToSetActive();
            if (mappingToApply != null && DriverService.IsAvailable)
            {
                DriverService.Activate(mappingToApply, Devices.Elements);
            }
        }
    }
}
