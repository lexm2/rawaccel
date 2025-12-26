using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Common;
using userspace_backend.Common.AccelFormulas;
using userspace_backend.Data.Profiles.Accel;
using userspace_backend.Data.Profiles.Accel.Formula;
using userspace_backend.Driver.Types;
using userspace_backend.Model.EditableSettings;

namespace userspace_backend.Model.AccelDefinitions.Formula
{

    public interface IClassicAccelerationDefinitionModel : IAccelDefinitionModelSpecific<ClassicAccel>
    {
    }

    public class ClassicAccelerationDefinitionModel
        : EditableSettingsSelectable<ClassicAccel, FormulaAccel>,
        IClassicAccelerationDefinitionModel
    {
        public const string AccelerationDIKey = $"{nameof(ClassicAccelerationDefinitionModel)}.{nameof(Acceleration)}";
        public const string ExponentDIKey = $"{nameof(ClassicAccelerationDefinitionModel)}.{nameof(Exponent)}";
        public const string OffsetDIKey = $"{nameof(ClassicAccelerationDefinitionModel)}.{nameof(Offset)}";
        public const string CapDIKey = $"{nameof(ClassicAccelerationDefinitionModel)}.{nameof(CapDIKey)}";

        private readonly ILutComputer _lutComputer;

        public ClassicAccelerationDefinitionModel(
            ILutComputer lutComputer,
            [FromKeyedServices(AccelerationDIKey)]IEditableSettingSpecific<double> acceleration,
            [FromKeyedServices(ExponentDIKey)]IEditableSettingSpecific<double> exponent,
            [FromKeyedServices(OffsetDIKey)]IEditableSettingSpecific<double> offset,
            [FromKeyedServices(CapDIKey)]IEditableSettingSpecific<double> cap)
            : base([acceleration, exponent, offset, cap], [])
        {
            _lutComputer = lutComputer;
            Acceleration = acceleration;
            Exponent = exponent;
            Offset = offset;
            Cap = cap;
        }

        public IEditableSettingSpecific<double> Acceleration { get; set; }

        public IEditableSettingSpecific<double> Exponent { get; set; }

        public IEditableSettingSpecific<double> Offset { get; set;  }

        public IEditableSettingSpecific<double> Cap { get; set; }

        public DriverAccelArgs MapToDriver(bool gain)
        {
            var formula = new ClassicFormula(
                Acceleration.ModelValue,
                Exponent.ModelValue,
                Offset.ModelValue,
                Cap.ModelValue,
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

        public override ClassicAccel MapToData()
        {
            return new ClassicAccel()
            {
                Acceleration = Acceleration.ModelValue,
                Exponent = Exponent.ModelValue,
                Offset = Offset.ModelValue,
                Cap = Cap.ModelValue,
            };
        }

        protected override bool TryMapEditableSettingsFromData(ClassicAccel data)
        {
            return Acceleration.TryUpdateModelDirectly(data.Acceleration)
                & Exponent.TryUpdateModelDirectly(data.Exponent)
                & Offset.TryUpdateModelDirectly(data.Offset)
                & Cap.TryUpdateModelDirectly(data.Cap);
        }

        protected override bool TryMapEditableSettingsCollectionsFromData(ClassicAccel data)
        {
            return true;
        }
    }
}
