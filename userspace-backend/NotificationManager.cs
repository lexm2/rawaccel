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

    public interface INotificationManager
    {
        event EventHandler<NotificationEventArgs>? NotificationRequested;
        event EventHandler<NotificationEventArgs>? QueuedNotificationRequested;
        event EventHandler<ModalEventArgs>? QueuedModalRequested;

        void TriggerNotification(string messageKey, NotificationType type, params object[] formatArgs);
        void QueueNotification(string messageKey, NotificationType type, params object[] formatArgs);
        void QueueModal(string modalType, params object[] parameters);
    }

    public class NotificationManager : INotificationManager
    {
        public event EventHandler<NotificationEventArgs>? NotificationRequested;
        public event EventHandler<NotificationEventArgs>? QueuedNotificationRequested;
        public event EventHandler<ModalEventArgs>? QueuedModalRequested;

        public void TriggerNotification(string messageKey, NotificationType type, params object[] formatArgs)
        {
            NotificationRequested?.Invoke(this, new NotificationEventArgs
            {
                MessageKey = messageKey,
                Type = type,
                FormatArgs = formatArgs
            });
        }

        public void QueueNotification(string messageKey, NotificationType type, params object[] formatArgs)
        {
            QueuedNotificationRequested?.Invoke(this, new NotificationEventArgs
            {
                MessageKey = messageKey,
                Type = type,
                FormatArgs = formatArgs
            });
        }

        public void QueueModal(string modalType, params object[] parameters)
        {
            QueuedModalRequested?.Invoke(this, new ModalEventArgs
            {
                ModalType = modalType,
                Parameters = parameters
            });
        }
    }
}
