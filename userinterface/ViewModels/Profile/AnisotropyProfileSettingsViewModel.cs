using userinterface.ViewModels.Controls;
using BE = userspace_backend.Model.ProfileComponents;

namespace userinterface.ViewModels.Profile
{
    public partial class AnisotropyProfileSettingsViewModel : ViewModelBase
    {
        public AnisotropyProfileSettingsViewModel(BE.IAnisotropyModel anisotropyBE)
        {
            AnisotropyBE = anisotropyBE;
            DomainX = new LocalizedFieldBase(AnisotropyBE.DomainX);
            DomainY = new LocalizedFieldBase(AnisotropyBE.DomainY);
            RangeX = new LocalizedFieldBase(AnisotropyBE.RangeX);
            RangeY = new LocalizedFieldBase(AnisotropyBE.RangeY);
            LPNorm = new LocalizedFieldBase(AnisotropyBE.LPNorm);
        }

        protected BE.IAnisotropyModel AnisotropyBE { get; }

        public LocalizedFieldBase DomainX { get; set; }

        public LocalizedFieldBase DomainY { get; set; }

        public LocalizedFieldBase RangeX { get; set; }

        public LocalizedFieldBase RangeY { get; set; }

        public LocalizedFieldBase LPNorm { get; set; }
    }
}
