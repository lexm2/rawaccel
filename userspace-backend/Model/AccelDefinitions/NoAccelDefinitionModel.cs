using userspace_backend.Data.Profiles;
using userspace_backend.Data.Profiles.Accel;
using userspace_backend.Driver.Types;
using userspace_backend.Model.EditableSettings;

namespace userspace_backend.Model.AccelDefinitions
{
    public interface INoAccelDefinitionModel : IAccelDefinitionModelSpecific<NoAcceleration>
    {
    }

    public class NoAccelDefinitionModel : EditableSettingsSelectable<NoAcceleration, Acceleration>, INoAccelDefinitionModel
    {
        public NoAccelDefinitionModel()
            : base([], [])
        {
            NoAcceleration = new NoAcceleration();
        }

        public NoAcceleration NoAcceleration { get; protected set; }

        public DriverAccelArgs MapToDriver(bool gain)
        {
            return new DriverAccelArgs()
            {
                Mode = AccelMode.NoAccel,
                Gain = gain,
            };
        }

        public override NoAcceleration MapToData()
        {
            return NoAcceleration;
        }

        protected override bool TryMapEditableSettingsFromData(NoAcceleration data)
        {
            return true;
        }

        protected override bool TryMapEditableSettingsCollectionsFromData(NoAcceleration data)
        {
            return true;
        }
    }
}
