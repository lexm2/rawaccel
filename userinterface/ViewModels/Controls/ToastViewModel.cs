using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Messaging;
using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Models;
using userinterface.Services;
using userinterface.Services.Events;

namespace userinterface.ViewModels.Controls
{
    public partial class ToastViewModel : ViewModelBase, IRecipient<ToastRequestedEvent>, IRecipient<ToastDismissedEvent>, IDisposable
    {
        private readonly LocalizationService localizationService;

        [ObservableProperty]
        private bool isVisible;

        [ObservableProperty]
        private string message = string.Empty;

        [ObservableProperty]
        private ToastType type;

        [ObservableProperty]
        private double progress = 100;

        private CancellationTokenSource? animationCancellation;

        public ToastViewModel(LocalizationService localizationService)
        {
            this.localizationService = localizationService;

            WeakReferenceMessenger.Default.Register<ToastRequestedEvent>(this);
            WeakReferenceMessenger.Default.Register<ToastDismissedEvent>(this);

            CloseCommand = new RelayCommand(Close);
        }

        public ICommand CloseCommand { get; }

        public async void Receive(ToastRequestedEvent e)
        {
            await Dispatcher.UIThread.InvokeAsync(async () =>
            {
                animationCancellation?.Cancel();

                var localizedMessage = localizationService.GetText(e.MessageKey);
                if (e.FormatArgs.Length > 0)
                {
                    localizedMessage = string.Format(localizedMessage, e.FormatArgs);
                }

                Message = localizedMessage;
                Type = e.Type;
                IsVisible = true;
                Progress = 100;

                await StartProgressAnimation(e.Duration);
            });
        }

        public void Receive(ToastDismissedEvent e)
        {
            animationCancellation?.Cancel();
            Dispatcher.UIThread.Post(() =>
            {
                IsVisible = false;
                Progress = 0;
            });
        }

        private async Task StartProgressAnimation(TimeSpan duration)
        {
            animationCancellation = new CancellationTokenSource();
            var token = animationCancellation.Token;

            try
            {
                var startTime = DateTime.UtcNow;
                var totalMilliseconds = duration.TotalMilliseconds;

                while (!token.IsCancellationRequested)
                {
                    var elapsed = DateTime.UtcNow - startTime;
                    var progressRatio = elapsed.TotalMilliseconds / totalMilliseconds;

                    if (progressRatio >= 1.0)
                    {
                        if (!token.IsCancellationRequested)
                        {
                            await Dispatcher.UIThread.InvokeAsync(() =>
                            {
                                Progress = 0;
                                if (IsVisible)
                                {
                                    WeakReferenceMessenger.Default.Send(new ToastDismissedEvent());
                                }
                            });
                        }
                        break;
                    }

                    var newProgress = 100 * (1.0 - progressRatio);
                    await Dispatcher.UIThread.InvokeAsync(() => Progress = newProgress);

                    await Task.Delay(8, token);
                }
            }
            catch (OperationCanceledException)
            {
            }
        }

        private void Close()
        {
            WeakReferenceMessenger.Default.Send(new ToastDismissedEvent());
        }

        public void Dispose()
        {
            animationCancellation?.Cancel();
            animationCancellation?.Dispose();
        }
    }
}
