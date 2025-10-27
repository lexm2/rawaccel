using System;

namespace userspace_backend
{
    public enum NotificationType
    {
        Info,
        Success,
        Warning,
        Error
    }

    public class NotificationEventArgs : EventArgs
    {
        public string MessageKey { get; set; } = string.Empty;
        public NotificationType Type { get; set; }
        public object[] FormatArgs { get; set; } = Array.Empty<object>();
    }

    public class ModalEventArgs : EventArgs
    {
        public string ModalType { get; set; } = string.Empty;
        public object[] Parameters { get; set; } = Array.Empty<object>();
    }

    public static class NotificationManager
    {
        public static event EventHandler<NotificationEventArgs>? NotificationRequested;
        public static event EventHandler<NotificationEventArgs>? QueuedNotificationRequested;
        public static event EventHandler<ModalEventArgs>? QueuedModalRequested;

        public static void TriggerNotification(string messageKey, NotificationType type, params object[] formatArgs)
        {
            NotificationRequested?.Invoke(null, new NotificationEventArgs
            {
                MessageKey = messageKey,
                Type = type,
                FormatArgs = formatArgs
            });
        }

        public static void QueueNotification(string messageKey, NotificationType type, params object[] formatArgs)
        {
            QueuedNotificationRequested?.Invoke(null, new NotificationEventArgs
            {
                MessageKey = messageKey,
                Type = type,
                FormatArgs = formatArgs
            });
        }

        public static void QueueModal(string modalType, params object[] parameters)
        {
            QueuedModalRequested?.Invoke(null, new ModalEventArgs
            {
                ModalType = modalType,
                Parameters = parameters
            });
        }
    }
}
