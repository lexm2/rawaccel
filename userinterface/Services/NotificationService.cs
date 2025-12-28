using System;
using System.Threading;
using userinterface.Models;
using userinterface.Services.Events;

namespace userinterface.Services
{
    public class NotificationService : INotificationService
    {
        private Timer? timer;
        private readonly IEventBus eventBus;
        private readonly LocalizationService localizationService;
        private readonly ISettingsService settingsService;

        public NotificationService(
            IEventBus eventBus,
            LocalizationService localizationService,
            ISettingsService settingsService)
        {
            this.eventBus = eventBus;
            this.localizationService = localizationService;
            this.settingsService = settingsService;
        }

        public void ShowToast(string messageKey, ToastType type, int durationMs = 5000)
        {
            ShowToast(messageKey, type, durationMs, Array.Empty<object>());
        }

        public void ShowToast(string messageKey, ToastType type, int durationMs = 5000, params object[] formatArgs)
        {
            if (!settingsService.ShowToastNotifications)
            {
                return;
            }

            timer?.Dispose();

            eventBus.Publish(new ToastRequestedEvent(
                messageKey,
                type,
                TimeSpan.FromMilliseconds(durationMs),
                formatArgs
            ));

            timer = new Timer(state => HideToast(), null, durationMs, Timeout.Infinite);
        }

        public void HideToast()
        {
            timer?.Dispose();
            eventBus.Publish(new ToastDismissedEvent());
        }

        public void ShowSuccessToast(string messageKey, int durationMs = 5000)
        {
            ShowToast(messageKey, ToastType.Success, durationMs);
        }

        public void ShowSuccessToast(string messageKey, int durationMs = 5000, params object[] formatArgs)
        {
            ShowToast(messageKey, ToastType.Success, durationMs, formatArgs);
        }

        public void ShowErrorToast(string messageKey, int durationMs = 8000)
        {
            ShowToast(messageKey, ToastType.Error, durationMs);
        }

        public void ShowErrorToast(string messageKey, int durationMs = 8000, params object[] formatArgs)
        {
            ShowToast(messageKey, ToastType.Error, durationMs, formatArgs);
        }

        public void ShowWarningToast(string messageKey, int durationMs = 6000)
        {
            ShowToast(messageKey, ToastType.Warning, durationMs);
        }

        public void ShowWarningToast(string messageKey, int durationMs = 6000, params object[] formatArgs)
        {
            ShowToast(messageKey, ToastType.Warning, durationMs, formatArgs);
        }

        public void ShowInfoToast(string messageKey, int durationMs = 4000)
        {
            ShowToast(messageKey, ToastType.Info, durationMs);
        }

        public void ShowInfoToast(string messageKey, int durationMs = 4000, params object[] formatArgs)
        {
            ShowToast(messageKey, ToastType.Info, durationMs, formatArgs);
        }

        public void Dispose()
        {
            timer?.Dispose();
        }
    }
}
