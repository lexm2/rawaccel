using Avalonia.Controls;
using Avalonia.Media;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using userinterface.Services;
using userspace_backend.Logging;

namespace userinterface.Animations;

public class DeviceListAnimationHelper : CollectionAnimationHelperBase<Control>
{
    private readonly ListBox deviceListBox;

    public DeviceListAnimationHelper(
        ListBox deviceListBox,
        IFrameTimerService frameTimer,
        IAnimationStateService animationStateService,
        ILoggingService? loggingService = null)
        : base(frameTimer, animationStateService, loggingService)
    {
        this.deviceListBox = deviceListBox ?? throw new ArgumentNullException(nameof(deviceListBox));
    }

    public override int GetItemCount() => deviceListBox.ItemCount;

    public override Control? GetContainerAtIndex(int index)
    {
        if (index < 0 || index >= deviceListBox.ItemCount)
            return null;

        var container = deviceListBox.ContainerFromIndex(index) as Control;
        if (container == null && SupportsVirtualization)
        {
            loggingService?.LogDebug(LogSource.UI, "Device container at index {Index} is virtualized", index);
        }
        return container;
    }

    protected override string GetAnimationContext() => "DeviceList";

    protected override bool SupportsVirtualization => true;

    public override void UpdateInteractionState(bool enabled)
    {
    }

    public override double CalculatePositionForIndex(int index)
    {
        return 0;
    }

    public async ValueTask SlideAndFadeInAsync(int index, int staggerIndex = 0)
    {
        var container = GetContainerAtIndex(index);
        if (container == null) return;

        EnsureInitialPosition(container, SlideDirection.Up);

        await AnimateSlideAndFadeAsync(
            index,
            SlideDirection.Up,
            1.0,
            animationStateService.Config.AnimationDurationMs,
            staggerIndex
        );
    }

    public async ValueTask SlideAndFadeOutAsync(int index, SlideDirection direction, int staggerIndex = 0)
    {
        await AnimateSlideAndFadeAsync(
            index,
            direction,
            0.0,
            animationStateService.Config.DeleteAnimationDurationMs,
            staggerIndex
        );
    }

    public async ValueTask FadeOutAsync(int index, int staggerIndex = 0)
    {
        await AnimateOpacityAsync(
            index,
            0.0,
            animationStateService.Config.HideOthersAnimationDurationMs,
            staggerIndex
        );
    }

    public async ValueTask FadeInAsync(int index, int staggerIndex = 0)
    {
        var container = GetContainerAtIndex(index);
        if (container == null) return;

        EnsureInitialPosition(container, SlideDirection.Up);

        await AnimateSlideAndFadeAsync(
            index,
            SlideDirection.Up,
            1.0,
            animationStateService.Config.AnimationDurationMs,
            staggerIndex
        );
    }

    public async ValueTask FadeOutAllExceptAsync(int exceptIndex)
    {
        var tasks = new List<Task>();
        var itemCount = GetItemCount();

        for (int i = 0; i < itemCount; i++)
        {
            if (i == exceptIndex) continue;

            var container = GetContainerAtIndex(i);
            if (container == null) continue;

            tasks.Add(FadeOutAsync(i).AsTask());
        }

        if (tasks.Count > 0)
        {
            await Task.WhenAll(tasks);
        }
    }

    public async ValueTask AnimateAllDevicesInAsync()
    {
        frameTimer.StartMonitoring("AnimateAllDevicesIn");
        var tasks = new List<Task>();
        try
        {
            var itemCount = GetItemCount();

            for (int i = 0; i < itemCount; i++)
            {
                var container = GetContainerAtIndex(i);
                if (container == null) continue;

                int staggerIndex = Math.Min(i, 3);
                tasks.Add(FadeInAsync(i, staggerIndex).AsTask());
            }

            if (tasks.Count > 0)
            {
                animationStateService.SetAnimationsActive(true);
                try
                {
                    await Task.WhenAll(tasks);
                }
                finally
                {
                    animationStateService.SetAnimationsActive(false);
                }
            }
        }
        finally
        {
            frameTimer.StopMonitoring("AnimateAllDevicesIn");
        }
    }

    private void EnsureInitialPosition(Control container, SlideDirection direction)
    {
        var transform = container.RenderTransform as TranslateTransform;
        if (transform == null)
        {
            transform = new TranslateTransform();
            container.RenderTransform = transform;
        }

        var distance = GetSlideDistance(direction);
        switch (direction)
        {
            case SlideDirection.Up:
                transform.X = 0;
                transform.Y = distance;
                break;
            case SlideDirection.Down:
                transform.X = 0;
                transform.Y = -distance;
                break;
            case SlideDirection.Left:
                transform.X = distance;
                transform.Y = 0;
                break;
            case SlideDirection.Right:
                transform.X = -distance;
                transform.Y = 0;
                break;
        }

        container.Opacity = 0;
    }
}
