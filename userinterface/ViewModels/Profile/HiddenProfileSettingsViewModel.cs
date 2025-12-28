using userinterface.ViewModels.Controls;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Profile
{
    public partial class HiddenProfileSettingsViewModel : ViewModelBase
    {
        public HiddenProfileSettingsViewModel(BE.ProfileComponents.IHiddenModel hiddenBE)
        {
            HiddenBE = hiddenBE;
            RotationField = new LocalizedFieldBase(hiddenBE.RotationDegrees);
            SpeedCapField = new LocalizedFieldBase(hiddenBE.SpeedCap);
            LRRatioField = new LocalizedFieldBase(hiddenBE.LeftRightRatio);
            UDRatioField = new LocalizedFieldBase(hiddenBE.UpDownRatio);
            AngleSnappingField = new LocalizedFieldBase(hiddenBE.AngleSnappingDegrees);
            OutputSmoothingHalfLifeField = new LocalizedFieldBase(hiddenBE.OutputSmoothingHalfLife);
        }

        protected BE.ProfileComponents.IHiddenModel HiddenBE { get; }

        public LocalizedFieldBase RotationField { get; set; }

        public LocalizedFieldBase SpeedCapField { get; set; }

        public LocalizedFieldBase LRRatioField { get; set; }

        public LocalizedFieldBase UDRatioField { get; set; }

        public LocalizedFieldBase AngleSnappingField { get; set; }

        public LocalizedFieldBase OutputSmoothingHalfLifeField { get; set; }
    }
}
