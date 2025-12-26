using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Common;
using userspace_backend.Common.AccelFormulas;
using userspace_backend.Data.Profiles.Accel;
using userspace_backend.Data.Profiles.Accel.Formula;
using userspace_backend.Driver.Types;
using userspace_backend.Model.EditableSettings;

namespace userspace_backend.Model.AccelDefinitions.Formula
{
    public interface IJumpAccelerationDefinitionModel : IAccelDefinitionModelSpecific<JumpAccel>
    {
    }

    public class JumpAccelerationDefinitionModel
        : EditableSettingsSelectable<JumpAccel, FormulaAccel>,
        IJumpAccelerationDefinitionModel
    {
        public const string SmoothDIKey = $"{nameof(ClassicAccelerationDefinitionModel)}.{nameof(Smooth)}";
        public const string InputDIKey = $"{nameof(ClassicAccelerationDefinitionModel)}.{nameof(Input)}";
        public const string OutputDIKey = $"{nameof(ClassicAccelerationDefinitionModel)}.{nameof(Output)}";

        private readonly ILutComputer _lutComputer;

        public JumpAccelerationDefinitionModel(
            ILutComputer lutComputer,
            [FromKeyedServices(SmoothDIKey)]IEditableSettingSpecific<double> smooth,
            [FromKeyedServices(InputDIKey)]IEditableSettingSpecific<double> input,
            [FromKeyedServices(OutputDIKey)]IEditableSettingSpecific<double> output)
            : base([smooth, input, output], [])
        {
            _lutComputer = lutComputer;
            Smooth = smooth;
            Input = input;
            Output = output;
        }

        public IEditableSettingSpecific<double> Smooth { get; set; }

        public IEditableSettingSpecific<double> Input { get; set; }

        public IEditableSettingSpecific<double> Output { get; set; }

        public DriverAccelArgs MapToDriver(bool gain)
        {
            var formula = new JumpFormula(
                Input.ModelValue,
                Output.ModelValue,
                Smooth.ModelValue,
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

        public override JumpAccel MapToData()
        {
            return new JumpAccel()
            {
                Smooth = Smooth.ModelValue,
                Input = Input.ModelValue,
                Output = Output.ModelValue
            };
        }

        protected override bool TryMapEditableSettingsFromData(JumpAccel data)
        {
            return Smooth.TryUpdateModelDirectly(data.Smooth)
                & Input.TryUpdateModelDirectly(data.Input)
                & Output.TryUpdateModelDirectly(data.Output);
        }

        protected override bool TryMapEditableSettingsCollectionsFromData(JumpAccel data)
        {
            return true;
        }
    }
}
