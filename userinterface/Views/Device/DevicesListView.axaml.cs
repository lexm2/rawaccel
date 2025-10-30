using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;
using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.Specialized;
using System.Threading.Tasks;
using userinterface.Animations;
using userinterface.Services;
using userinterface.ViewModels.Device;
using userspace_backend.Logging;

namespace userinterface.Views.Device;

public partial class DevicesListView : UserControl
{
    private DevicesListViewModel? viewModel;
    private int lastKnownItemCount = 0;
    private bool isInitialLoad = true;

    private readonly IAnimationStateService animationStateService;
    private readonly IFrameTimerService frameTimer;
    private readonly ILoggingService? loggingService;
    private DeviceListAnimationHelper? animationHelper;

    public bool AreAnimationsActive => animationStateService.AreAnimationsActive;

    public DevicesListView()
    {
        animationStateService = App.Services?.GetRequiredService<IAnimationStateService>() ?? throw new InvalidOperationException("AnimationStateService not available");
        frameTimer = App.Services?.GetRequiredService<IFrameTimerService>() ?? throw new InvalidOperationException("FrameTimerService not available");
        loggingService = App.Services?.GetService(typeof(ILoggingService)) as ILoggingService;

        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
        DevicesListInView.ContainerPrepared += OnContainerPrepared;
    }

    private void OnDeviceDeleteConfirmed(object? sender, DeviceViewModel.DeviceDeleteConfirmedEventArgs e)
    {
        _ = AnimateDeviceDelete(e.Device);
    }

    private async Task AnimateDeviceDelete(DeviceViewModel deviceViewModel)
    {
        if (viewModel == null) return;

        int index = viewModel.DeviceViews.IndexOf(deviceViewModel);
        if (index < 0) return;

        loggingService?.LogDebug(LogSource.UI, "AnimateDeviceDelete: Deleting device at index {Index}, total devices before: {Count}", index, viewModel.DeviceViews.Count);

        var container = DevicesListInView.ContainerFromIndex(index) as Control;
        if (container != null)
        {

            try
            {
                await HideAllOtherDevices(index);
                await AnimateDeviceOut(container, index);

                await Dispatcher.UIThread.InvokeAsync(() =>
                {
                    deviceViewModel.DeleteSelf();
                });

                loggingService?.LogDebug(LogSource.UI, "AnimateDeviceDelete: Device deleted, remaining devices: {Count}", viewModel.DeviceViews.Count);
                await AnimateAllDevicesIn();
            }
            catch (Exception ex)
            {
                loggingService?.LogError(LogSource.UI, ex, "Animation error during device delete");
                deviceViewModel.DeleteSelf();
            }
        }
        else
        {
            deviceViewModel.DeleteSelf();
        }
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is DevicesListViewModel vm)
        {
            if (viewModel != null)
            {
                viewModel.DeviceViews.CollectionChanged -= OnDevicesCollectionChanged;
            }

            viewModel = vm;
            lastKnownItemCount = vm.DeviceViews.Count;

            if (animationHelper == null)
            {
                animationHelper = new DeviceListAnimationHelper(
                    DevicesListInView,
                    frameTimer,
                    animationStateService,
                    loggingService
                );
            }

            vm.DeviceViews.CollectionChanged += OnDevicesCollectionChanged;

            _ = Task.Run(async () =>
            {
                await Task.Delay(vm.DeviceViews.Count * animationStateService.Config.StaggerDelayMs + animationStateService.Config.AnimationDurationMs);
                isInitialLoad = false;
            });
        }
    }

    private void OnLoaded(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        _ = Task.Run(async () =>
        {
            await Task.Delay(100);
            await Dispatcher.UIThread.InvokeAsync(() =>
            {
                isInitialLoad = false;
            });
        });
    }

    private void OnContainerPrepared(object? sender, ContainerPreparedEventArgs e)
    {
        if (e.Container is Control container && viewModel != null)
        {
            bool isNewItem = e.Index >= lastKnownItemCount && !isInitialLoad;
            bool isLastElement = e.Index == viewModel.DeviceViews.Count - 1;

            var currentTransform = container.RenderTransform as TranslateTransform;
            loggingService?.LogDebug(LogSource.UI, "OnContainerPrepared: Index {Index}, IsNewItem: {IsNew}, IsLast: {IsLast}, IsInitialLoad: {IsInitial}, CurrentTransform: ({X},{Y})",
                e.Index, isNewItem, isLastElement, isInitialLoad,
                currentTransform?.X ?? 0, currentTransform?.Y ?? 0);

            if (e.Index >= 0 && e.Index < viewModel.DeviceViews.Count)
            {
                var deviceViewModel = viewModel.DeviceViews[e.Index];
                deviceViewModel.DeleteConfirmed -= OnDeviceDeleteConfirmed;
                deviceViewModel.DeleteConfirmed += OnDeviceDeleteConfirmed;
            }

            if (isInitialLoad)
            {
                container.Opacity = 0;
                container.RenderTransform = new TranslateTransform(0, animationStateService.Config.SlideUpDistance);

                _ = Task.Run(async () =>
                {
                    int delay = e.Index * animationStateService.Config.StaggerDelayMs;
                    await Task.Delay(delay);
                    await ShowDevice(container, e.Index);
                });
            }
            else if (isNewItem)
            {
                container.Opacity = 0;
                container.RenderTransform = new TranslateTransform(0, animationStateService.Config.SlideUpDistance);

                _ = Task.Run(async () =>
                {
                    await Task.Delay(50);
                    await AnimateDeviceIn(container, e.Index);
                });
            }
            else
            {
                container.Opacity = 1;
                container.RenderTransform = new TranslateTransform(0, 0);
            }
        }
    }

    private void OnDevicesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.Action == NotifyCollectionChangedAction.Add && e.NewItems != null && e.NewItems.Count > 0)
        {
            _ = Task.Run(async () =>
            {
                await Task.Delay(200);
                await Dispatcher.UIThread.InvokeAsync(() =>
                {
                    lastKnownItemCount = viewModel?.DeviceViews.Count ?? 0;
                });
            });
        }
    }

    private async Task AnimateDeviceIn(Control container, int index)
    {
        if (animationHelper == null) return;
        await animationHelper.SlideAndFadeInAsync(index);
    }

    private async Task AnimateDeviceOut(Control container, int index)
    {
        if (animationHelper == null) return;
        await animationHelper.SlideAndFadeOutAsync(index, SlideDirection.Left);
    }

    private async Task HideAllOtherDevices(int exceptIndex)
    {
        if (animationHelper == null) return;
        await animationHelper.FadeOutAllExceptAsync(exceptIndex);
    }

    private async Task AnimateAllDevicesIn()
    {
        if (animationHelper == null) return;

        loggingService?.LogDebug(LogSource.UI, "AnimateAllDevicesIn: Starting animation for {Count} devices", viewModel?.DeviceViews.Count ?? 0);

        await Task.Delay(100);
        await animationHelper.AnimateAllDevicesInAsync();
    }

    private async Task ShowDevice(Control container, int index)
    {
        if (animationHelper == null) return;
        await animationHelper.FadeInAsync(index);
    }

}