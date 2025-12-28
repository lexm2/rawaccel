using userinterface.Services;
using userinterface.ViewModels.Controls;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Profile
{
    public partial class ProfileSettingsViewModel : ViewModelBase
    {
        private readonly INotificationService notificationService;

        public ProfileSettingsViewModel(INotificationService notificationService)
        {
            this.notificationService = notificationService;
        }

        protected BE.IProfileModel ProfileModelBE { get; private set; } = null!;

        public LocalizedFieldBase OutputDPIField { get; private set; } = null!;

        public LocalizedFieldBase YXRatioField { get; private set; } = null!;

        public AccelerationProfileSettingsViewModel AccelerationSettings { get; private set; } = null!;

        public HiddenProfileSettingsViewModel HiddenSettings { get; private set; } = null!;

        public void Initialize(BE.IProfileModel profileModel)
        {
            ProfileModelBE = profileModel;
            OutputDPIField = new LocalizedFieldBase(profileModel.OutputDPI);
            YXRatioField = new LocalizedFieldBase(profileModel.YXRatio);
            AccelerationSettings = new AccelerationProfileSettingsViewModel(profileModel.Acceleration, notificationService);
            HiddenSettings = new HiddenProfileSettingsViewModel(profileModel.Hidden);
        }
    }
}
