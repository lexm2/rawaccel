using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Data;
using userspace_backend.Model.EditableSettings;

namespace userspace_backend.Model
{
    public interface IDeviceModel : IEditableSettingsCollectionSpecific<Device>
    {
        IEditableSettingSpecific<string> Name { get; }

        IEditableSettingSpecific<string> HardwareID { get; }

        IEditableSettingSpecific<int> DPI { get; }

        IEditableSettingSpecific<int> PollRate { get; }

        IEditableSettingSpecific<bool> Ignore { get; }

        IEditableSettingSpecific<string> DeviceGroup { get; }
    }

    public class DeviceModel : NamedEditableSettingsCollection<Device>, IDeviceModel
    {
        public const string NameDIKey = $"{nameof(DeviceModel)}.{nameof(Name)}";
        public const string HardwareIDDIKey = $"{nameof(DeviceModel)}.{nameof(HardwareID)}";
        public const string DPIDIKey = $"{nameof(DeviceModel)}.{nameof(DPI)}";
        public const string PollRateDIKey = $"{nameof(DeviceModel)}.{nameof(PollRate)}";
        public const string IgnoreDIKey = $"{nameof(DeviceModel)}.{nameof(Ignore)}";
        public const string DeviceGroupDIKey = $"{nameof(DeviceModel)}.{nameof(DeviceGroup)}";

        public DeviceModel(
            [FromKeyedServices(NameDIKey)] IEditableSettingSpecific<string> name,
            [FromKeyedServices(HardwareIDDIKey)] IEditableSettingSpecific<string> hardwareID,
            [FromKeyedServices(DPIDIKey)] IEditableSettingSpecific<int> dpi,
            [FromKeyedServices(PollRateDIKey)] IEditableSettingSpecific<int> pollRate,
            [FromKeyedServices(IgnoreDIKey)] IEditableSettingSpecific<bool> ignore,
            [FromKeyedServices(DeviceGroupDIKey)] IEditableSettingSpecific<string> deviceGroup)
            : base(name, [hardwareID, dpi, pollRate, ignore, deviceGroup], [])
        {
            HardwareID = hardwareID;
            DPI = dpi;
            PollRate = pollRate;
            Ignore = ignore;
            DeviceGroup = deviceGroup;
        }

        public IEditableSettingSpecific<string> HardwareID { get; protected set; }

        public IEditableSettingSpecific<int> DPI { get; protected set; }

        public IEditableSettingSpecific<int> PollRate { get; protected set; }

        public IEditableSettingSpecific<bool> Ignore { get; protected set; }

        public IEditableSettingSpecific<string> DeviceGroup { get; set; }

        public override Device MapToData()
        {
            return new Device()
            {
                Name = Name.ModelValue,
                HWID = HardwareID.ModelValue,
                DPI = DPI.ModelValue,
                PollingRate = PollRate.ModelValue,
                Ignore = Ignore.ModelValue,
                DeviceGroup = DeviceGroup.ModelValue,
            };
        }

        protected override bool TryMapEditableSettingsFromData(Device data)
        {
            return Name.TryUpdateModelDirectly(data.Name)
                & HardwareID.TryUpdateModelDirectly(data.HWID)
                & DPI.TryUpdateModelDirectly(data.DPI)
                & PollRate.TryUpdateModelDirectly(data.PollingRate)
                & Ignore.TryUpdateModelDirectly(data.Ignore)
                & DeviceGroup.TryUpdateModelDirectly(data.DeviceGroup);
        }

        protected override bool TryMapEditableSettingsCollectionsFromData(Device data)
        {
            return true;
        }
    }
}
