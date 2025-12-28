using System;
using System.Collections.ObjectModel;
using System.Linq;
using userinterface.Services;
using userinterface.ViewModels.Controls;
using BE = userspace_backend.Model.AccelDefinitions;
using BEData = userspace_backend.Data.Profiles.Accel.FormulaAccel;

namespace userinterface.ViewModels.Profile
{
    public class AccelerationFormulaSettingsViewModel : ViewModelBase
    {
        private readonly INotificationService notificationService;

        public static ObservableCollection<string> FormulaTypes { get; } =
            new(Enum.GetValues(typeof(BEData.AccelerationFormulaType))
                .Cast<BEData.AccelerationFormulaType>()
                .Select(formulaType => formulaType.ToString()));

        public static ObservableCollection<string> FormulaTypeKeys { get; } =
            new(Enum.GetValues(typeof(BEData.AccelerationFormulaType))
                .Cast<BEData.AccelerationFormulaType>()
                .Select(formulaType => $"AccelFormula{formulaType}"));

        public AccelerationFormulaSettingsViewModel(BE.IFormulaAccelModel formulaAccel, INotificationService notificationService)
        {
            this.notificationService = notificationService;
            FormulaAccelBE = formulaAccel;

            SynchronousSettings = new SynchronousSettings((formulaAccel.GetSelectable(BEData.AccelerationFormulaType.Synchronous)
                as BE.Formula.ISynchronousAccelerationDefinitionModel)!);

            LinearSettings = new LinearSettings((formulaAccel.GetSelectable(BEData.AccelerationFormulaType.Linear)
                as BE.Formula.LinearAccelerationDefinitionModel)!);

            ClassicSettings = new ClassicSettings((formulaAccel.GetSelectable(BEData.AccelerationFormulaType.Classic)
                as BE.Formula.ClassicAccelerationDefinitionModel)!);

            PowerSettings = new PowerSettings((formulaAccel.GetSelectable(BEData.AccelerationFormulaType.Power)
                as BE.Formula.PowerAccelerationDefinitionModel)!);

            NaturalSettings = new NaturalSettings((formulaAccel.GetSelectable(BEData.AccelerationFormulaType.Natural)
                as BE.Formula.NaturalAccelerationDefinitionModel)!);

            JumpSettings = new JumpSettings((formulaAccel.GetSelectable(BEData.AccelerationFormulaType.Jump)
                as BE.Formula.JumpAccelerationDefinitionModel)!);

            ClassicSettings.Exponent.PropertyChanged += OnClassicExponentChanged;
        }

        public BE.IFormulaAccelModel FormulaAccelBE { get; }

        public static ObservableCollection<string> FormulaTypesLocal => FormulaTypes;

        public static ObservableCollection<string> FormulaTypeKeysLocal => FormulaTypeKeys;

        public SynchronousSettings SynchronousSettings { get; }

        public LinearSettings LinearSettings { get; }

        public ClassicSettings ClassicSettings { get; }

        public PowerSettings PowerSettings { get; }

        public NaturalSettings NaturalSettings { get; }

        public JumpSettings JumpSettings { get; }

        private void OnClassicExponentChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(LocalizedFieldBase.ValueText) &&
                FormulaAccelBE.FormulaType.InterfaceValue == BEData.AccelerationFormulaType.Classic.ToString())
            {
                if (double.TryParse(ClassicSettings.Exponent.ValueText, out double exponentValue) &&
                    Math.Abs(exponentValue - 2.0) < 0.001)
                {
                    notificationService.ShowInfoToast("ProfileClassicLinearEquivalent");
                }
            }
        }
    }

    public class SynchronousSettings
    {
        public SynchronousSettings(BE.Formula.ISynchronousAccelerationDefinitionModel synchronousAccelModelBE)
        {
            SyncSpeed = new LocalizedFieldBase(synchronousAccelModelBE.SyncSpeed);
            Motivity = new LocalizedFieldBase(synchronousAccelModelBE.Motivity);
            Gamma = new LocalizedFieldBase(synchronousAccelModelBE.Gamma);
            Smoothness = new LocalizedFieldBase(synchronousAccelModelBE.Smoothness);
        }

        public LocalizedFieldBase SyncSpeed { get; set; }

        public LocalizedFieldBase Motivity { get; set; }

        public LocalizedFieldBase Gamma { get; set; }

        public LocalizedFieldBase Smoothness { get; set; }
    }

    public class LinearSettings
    {
        public LinearSettings(BE.Formula.LinearAccelerationDefinitionModel linearAccelModelBE)
        {
            Acceleration = new LocalizedFieldBase(linearAccelModelBE.Acceleration);
            Offset = new LocalizedFieldBase(linearAccelModelBE.Offset);
            Cap = new LocalizedFieldBase(linearAccelModelBE.Cap);
        }

        public LocalizedFieldBase Acceleration { get; set; }

        public LocalizedFieldBase Offset { get; set; }

        public LocalizedFieldBase Cap { get; set; }
    }

    public class ClassicSettings
    {
        public ClassicSettings(BE.Formula.ClassicAccelerationDefinitionModel classicAccelModelBE)
        {
            Acceleration = new LocalizedFieldBase(classicAccelModelBE.Acceleration);
            Exponent = new LocalizedFieldBase(classicAccelModelBE.Exponent);
            Offset = new LocalizedFieldBase(classicAccelModelBE.Offset);
            Cap = new LocalizedFieldBase(classicAccelModelBE.Cap);
        }

        public LocalizedFieldBase Acceleration { get; set; }

        public LocalizedFieldBase Exponent { get; set; }

        public LocalizedFieldBase Offset { get; set; }

        public LocalizedFieldBase Cap { get; set; }
    }

    public class PowerSettings
    {
        public PowerSettings(BE.Formula.PowerAccelerationDefinitionModel powerAccelModelBE)
        {
            Scale = new LocalizedFieldBase(powerAccelModelBE.Scale);
            Exponent = new LocalizedFieldBase(powerAccelModelBE.Exponent);
            OutputOffset = new LocalizedFieldBase(powerAccelModelBE.OutputOffset);
            Cap = new LocalizedFieldBase(powerAccelModelBE.Cap);
        }

        public LocalizedFieldBase Scale { get; set; }

        public LocalizedFieldBase Exponent { get; set; }

        public LocalizedFieldBase OutputOffset { get; set; }

        public LocalizedFieldBase Cap { get; set; }
    }

    public class NaturalSettings
    {
        public NaturalSettings(BE.Formula.NaturalAccelerationDefinitionModel naturalAccelModelBE)
        {
            DecayRate = new LocalizedFieldBase(naturalAccelModelBE.DecayRate);
            InputOffset = new LocalizedFieldBase(naturalAccelModelBE.InputOffset);
            Limit = new LocalizedFieldBase(naturalAccelModelBE.Limit);
        }

        public LocalizedFieldBase DecayRate { get; set; }

        public LocalizedFieldBase InputOffset { get; set; }

        public LocalizedFieldBase Limit { get; set; }
    }

    public class JumpSettings
    {
        public JumpSettings(BE.Formula.JumpAccelerationDefinitionModel jumpAccelModelBE)
        {
            Smooth = new LocalizedFieldBase(jumpAccelModelBE.Smooth);
            Input = new LocalizedFieldBase(jumpAccelModelBE.Input);
            Output = new LocalizedFieldBase(jumpAccelModelBE.Output);
        }

        public LocalizedFieldBase Smooth { get; set; }

        public LocalizedFieldBase Input { get; set; }

        public LocalizedFieldBase Output { get; set; }
    }
}
