using userinterface.Services;
using userinterface.ViewModels.Fields;
using userspace_backend.Logging;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Profile
{
    public partial class ProfileSettingsViewModel : ViewModelBase
    {
        private readonly INotificationService notificationService;
        private readonly LocalizationService localizationService;
        private readonly IModalService modalService;
        private readonly ILoggingService loggingService;
        private readonly userspace_backend.INotificationManager notificationManager;

        public ProfileSettingsViewModel(INotificationService notificationService, LocalizationService localizationService, IModalService modalService, ILoggingService loggingService, userspace_backend.INotificationManager notificationManager)
        {
            this.notificationService = notificationService;
            this.localizationService = localizationService;
            this.modalService = modalService;
            this.loggingService = loggingService;
            this.notificationManager = notificationManager;
        }

        protected BE.IProfileModel ProfileModelBE { get; private set; } = null!;

        public EditableFieldViewModel OutputDPIField { get; private set; } = null!;

        public EditableFieldViewModel YXRatioField { get; private set; } = null!;

        public AccelerationProfileSettingsViewModel AccelerationSettings { get; private set; } = null!;

        public HiddenProfileSettingsViewModel HiddenSettings { get; private set; } = null!;

        public void Initialize(BE.IProfileModel profileModel)
        {
            ProfileModelBE = profileModel;
            OutputDPIField = new EditableFieldViewModel(profileModel.OutputDPI);
            YXRatioField = new EditableFieldViewModel(profileModel.YXRatio);
            AccelerationSettings = new AccelerationProfileSettingsViewModel(profileModel.Acceleration, notificationService, localizationService, modalService, loggingService, notificationManager);
            HiddenSettings = new HiddenProfileSettingsViewModel(profileModel.Hidden);
        }
    }
}