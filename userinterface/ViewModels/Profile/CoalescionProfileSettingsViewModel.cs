using userinterface.ViewModels.Controls;
using BE = userspace_backend.Model.ProfileComponents;

namespace userinterface.ViewModels.Profile
{
    public partial class CoalescionProfileSettingsViewModel : ViewModelBase
    {
        public CoalescionProfileSettingsViewModel(BE.ICoalescionModel coalescionBE)
        {
            CoalescionBE = coalescionBE;
            InputSmoothingHalfLife = new LocalizedFieldBase(coalescionBE.InputSmoothingHalfLife);
            ScaleSmoothingHalfLife = new LocalizedFieldBase(coalescionBE.ScaleSmoothingHalfLife);
        }

        protected BE.ICoalescionModel CoalescionBE { get; }

        public LocalizedFieldBase InputSmoothingHalfLife { get; set; }

        public LocalizedFieldBase ScaleSmoothingHalfLife { get; set; }
    }
}
