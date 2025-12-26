using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Common;
using userspace_backend.Common.AccelFormulas;
using userspace_backend.Data.Profiles.Accel;
using userspace_backend.Data.Profiles.Accel.Formula;
using userspace_backend.Driver.Types;
using userspace_backend.Model.EditableSettings;

namespace userspace_backend.Model.AccelDefinitions.Formula
{
    public interface INaturalAccelerationDefinitionModel : IAccelDefinitionModelSpecific<NaturalAccel>
    {
    }

    public class NaturalAccelerationDefinitionModel
        : EditableSettingsSelectable<NaturalAccel, FormulaAccel>,
        INaturalAccelerationDefinitionModel
    {
        public const string DecayRateDIKey = $"{nameof(NaturalAccelerationDefinitionModel)}.{nameof(DecayRate)}";
        public const string InputOffsetDIKey = $"{nameof(NaturalAccelerationDefinitionModel)}.{nameof(InputOffset)}";
        public const string LimitDIKey = $"{nameof(NaturalAccelerationDefinitionModel)}.{nameof(Limit)}";

        private readonly ILutComputer _lutComputer;

        public NaturalAccelerationDefinitionModel(
            ILutComputer lutComputer,
            [FromKeyedServices(DecayRateDIKey)]IEditableSettingSpecific<double> decayRate,
            [FromKeyedServices(InputOffsetDIKey)]IEditableSettingSpecific<double> inputOffset,
            [FromKeyedServices(LimitDIKey)]IEditableSettingSpecific<double> limit)
            : base([decayRate, inputOffset, limit], [])
        {
            _lutComputer = lutComputer;
            DecayRate = decayRate;
            InputOffset = inputOffset;
            Limit = limit;
        }

        public IEditableSettingSpecific<double> DecayRate { get; set; }

        public IEditableSettingSpecific<double> InputOffset { get; set; }

        public IEditableSettingSpecific<double> Limit { get; set; }

        public DriverAccelArgs MapToDriver(bool gain)
        {
            var formula = new NaturalFormula(
                DecayRate.ModelValue,
                InputOffset.ModelValue,
                Limit.ModelValue,
                gain);

            var lut = _lutComputer.ComputeLut(formula);

            return new DriverAccelArgs
            {
                Mode = AccelMode.Lut,
                Gain = gain,
                LutData = lut.Data,
                LutLength = lut.Length
            };
        }

        public override NaturalAccel MapToData()
        {
            return new NaturalAccel()
            {
                DecayRate = DecayRate.ModelValue,
                InputOffset = InputOffset.ModelValue,
                Limit = Limit.ModelValue,
            };
        }

        protected override bool TryMapEditableSettingsFromData(NaturalAccel data)
        {
            return DecayRate.TryUpdateModelDirectly(data.DecayRate)
                & InputOffset.TryUpdateModelDirectly(data.InputOffset)
                & Limit.TryUpdateModelDirectly(data.Limit);
        }

        protected override bool TryMapEditableSettingsCollectionsFromData(NaturalAccel data)
        {
            return true;
        }
    }
}
