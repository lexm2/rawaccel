using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using userspace_backend.Model.EditableSettings;
using DATA = userspace_backend.Data;

namespace userspace_backend.Model
{
    public class MappingsModel : EditableSettingsCollection<DATA.MappingSet>
    {
        public MappingsModel(DATA.MappingSet dataObject, DeviceGroups deviceGroups, IProfilesModel profiles, IServiceProvider serviceProvider)
            : base(dataObject)
        {
            ServiceProvider = serviceProvider;
            DeviceGroups = deviceGroups;
            Profiles = profiles;
            NameValidator = new MappingNameValidator(this);
            InitMappings(dataObject);
        }

        public ObservableCollection<MappingModel> Mappings { get; protected set; }

        protected IServiceProvider ServiceProvider { get; }

        protected DeviceGroups DeviceGroups { get; }

        protected IProfilesModel Profiles { get; }

        protected MappingNameValidator NameValidator { get; }

        public MappingModel GetMappingToSetActive()
        {
            return Mappings.FirstOrDefault(m => m.SetActive);
        }

        public bool TryGetMapping(string name, out MappingModel? mapping)
        {
            mapping = Mappings.FirstOrDefault(
                m => string.Equals(m.Name.ModelValue, name, StringComparison.InvariantCultureIgnoreCase));

            return mapping is not null;
        }

        protected bool TryGetDefaultMapping([MaybeNullWhen(false)] out DATA.Mapping defaultMapping)
        {
            for (int i = 0; i < 10; i++)
            {
                string mappingNameToAdd = $"Mapping{i}";
                if (TryGetMapping(mappingNameToAdd, out _))
                {
                    continue;
                }

                defaultMapping = new()
                {
                    Name = mappingNameToAdd,
                    GroupsToProfiles = [],
                };

                return true;
            }

            defaultMapping = null;
            return false;
        }

        public bool TryAddMapping(DATA.Mapping? mappingToAdd = null)
        {
            if (mappingToAdd is null)
            {
                if (!TryGetDefaultMapping(out var defaultMapping))
                {
                    return false;
                }

                mappingToAdd = defaultMapping;
            }
            else if (TryGetMapping(mappingToAdd.Name, out _))
            {
                return false;
            }

            // Create Name setting for the mapping
            var nameSetting = new EditableSettingV2<string>(
                displayName: "Name",
                initialValue: mappingToAdd.Name,
                parser: ServiceProvider.GetRequiredService<IUserInputParser<string>>(),
                validator: NameValidator);

            // Construct MappingModel with DI pattern
            MappingModel mapping = new MappingModel(nameSetting, NameValidator, DeviceGroups, Profiles, mappingToAdd);
            Mappings.Add(mapping);
            return true;
        }

        public bool RemoveMapping(MappingModel mapping)
        {
            return Mappings.Remove(mapping);
        }

        public override DATA.MappingSet MapToData()
        {
            return new DATA.MappingSet()
            {
                Mappings = Mappings.Select(m => m.MapToData()).ToArray(),
            };
        }

        protected override IEnumerable<IEditableSetting> EnumerateEditableSettings()
        {
            return [];
        }

        protected override IEnumerable<IEditableSettingsCollectionV2> EnumerateEditableSettingsCollections()
        {
            return Mappings;
        }

        protected override void InitEditableSettingsAndCollections(DATA.MappingSet dataObject)
        {
            Mappings = new ObservableCollection<MappingModel>();
        }

        protected void InitMappings(DATA.MappingSet dataObject)
        {
            foreach (DATA.Mapping mapping in dataObject?.Mappings ?? [])
            {
                TryAddMapping(mapping);
            }
        }
    }

    public class MappingNameValidator(MappingsModel mappings) : IModelValueValidator<string>
    {
        protected MappingsModel Mappings { get; } = mappings;
        public bool Validate(string value)
        {
            return !Mappings.TryGetMapping(value, out _);
        }
    }
}
