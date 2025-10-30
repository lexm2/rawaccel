using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Styling;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace userinterface.Services.Animation;

public class AnimationService : IAnimationService
{
    private readonly IAnimationStateService animationStateService;

    public AnimationService(IAnimationStateService animationStateService)
    {
        this.animationStateService = animationStateService ?? throw new ArgumentNullException(nameof(animationStateService));
    }

    public async Task SlideInAsync(Control control, SlideDirection direction, CancellationToken cancellationToken)
    {
        var config = animationStateService.Config;
        var (axis, from, to) = GetSlideParameters(direction, true, config);

        var transform = animationStateService.EnsureTranslateTransform(control, axis == TransformAxis.X ? from : 0, axis == TransformAxis.Y ? from : 0);

        await animationStateService.AnimateTransformAsync(
            transform,
            axis,
            from,
            to,
            config.AnimationDurationMs,
            EaseOutBack,
            cancellationToken);
    }

    public async Task SlideOutAsync(Control control, SlideDirection direction, CancellationToken cancellationToken)
    {
        var config = animationStateService.Config;
        var (axis, from, to) = GetSlideParameters(direction, false, config);

        var transform = control.RenderTransform as TranslateTransform ?? animationStateService.EnsureTranslateTransform(control, 0, 0);

        await animationStateService.AnimateTransformAsync(
            transform,
            axis,
            from,
            to,
            config.DeleteAnimationDurationMs,
            EaseInQuad,
            cancellationToken);
    }

    public async Task FadeInAsync(Control control, int durationMs, CancellationToken cancellationToken)
    {
        var animation = animationStateService.CreateOpacityAnimation(0, 1, durationMs, new CubicEaseOut());
        await animation.RunAsync(control, cancellationToken);
    }

    public async Task FadeOutAsync(Control control, int durationMs, CancellationToken cancellationToken)
    {
        var animation = animationStateService.CreateOpacityAnimation(1, 0, durationMs, new CubicEaseIn());
        await animation.RunAsync(control, cancellationToken);
    }

    public async Task SlideAndFadeInAsync(Control control, SlideDirection direction, CancellationToken cancellationToken)
    {
        var config = animationStateService.Config;
        var (axis, from, to) = GetSlideParameters(direction, true, config);

        control.Opacity = 0;
        var transform = animationStateService.EnsureTranslateTransform(control, axis == TransformAxis.X ? from : 0, axis == TransformAxis.Y ? from : 0);

        var opacityAnimation = animationStateService.CreateOpacityAnimation(0, 1, config.AnimationDurationMs, new CubicEaseOut());
        var opacityTask = opacityAnimation.RunAsync(control, cancellationToken);
        var transformTask = animationStateService.AnimateTransformAsync(
            transform,
            axis,
            from,
            to,
            config.AnimationDurationMs,
            EaseOutBack,
            cancellationToken);

        await Task.WhenAll(opacityTask, transformTask);
    }

    public async Task SlideAndFadeOutAsync(Control control, SlideDirection direction, CancellationToken cancellationToken)
    {
        var config = animationStateService.Config;
        var (axis, from, to) = GetSlideParameters(direction, false, config);

        var transform = control.RenderTransform as TranslateTransform ?? animationStateService.EnsureTranslateTransform(control, 0, 0);

        var opacityAnimation = animationStateService.CreateOpacityAnimation(1, 0, config.DeleteAnimationDurationMs, new CubicEaseIn());
        var opacityTask = opacityAnimation.RunAsync(control, cancellationToken);
        var transformTask = animationStateService.AnimateTransformAsync(
            transform,
            axis,
            from,
            to,
            config.DeleteAnimationDurationMs,
            EaseInQuad,
            cancellationToken);

        await Task.WhenAll(opacityTask, transformTask);
    }

    public async Task ExpandAsync(Control control, double targetHeight, int durationMs, CancellationToken cancellationToken)
    {
        var animation = new Avalonia.Animation.Animation
        {
            Duration = TimeSpan.FromMilliseconds(durationMs),
            Easing = new CubicEaseOut(),
            Children =
            {
                new Avalonia.Animation.KeyFrame
                {
                    Cue = new Avalonia.Animation.Cue(0.0),
                    Setters = { new Avalonia.Setter(Control.HeightProperty, control.Height) }
                },
                new Avalonia.Animation.KeyFrame
                {
                    Cue = new Avalonia.Animation.Cue(1.0),
                    Setters = { new Avalonia.Setter(Control.HeightProperty, targetHeight) }
                }
            }
        };

        await animation.RunAsync(control, cancellationToken);
    }

    public async Task CollapseAsync(Control control, int durationMs, CancellationToken cancellationToken)
    {
        var currentHeight = control.Height;
        if (double.IsNaN(currentHeight) || currentHeight <= 0)
        {
            currentHeight = control.Bounds.Height;
        }

        var animation = new Avalonia.Animation.Animation
        {
            Duration = TimeSpan.FromMilliseconds(durationMs),
            Easing = new CubicEaseIn(),
            Children =
            {
                new Avalonia.Animation.KeyFrame
                {
                    Cue = new Avalonia.Animation.Cue(0.0),
                    Setters = { new Avalonia.Setter(Control.HeightProperty, currentHeight) }
                },
                new Avalonia.Animation.KeyFrame
                {
                    Cue = new Avalonia.Animation.Cue(1.0),
                    Setters = { new Avalonia.Setter(Control.HeightProperty, 0.0) }
                }
            }
        };

        await animation.RunAsync(control, cancellationToken);
    }

    public async Task AnimateToPositionAsync(Control control, double targetX, double targetY, int durationMs, CancellationToken cancellationToken)
    {
        var transform = control.RenderTransform as TranslateTransform ?? animationStateService.EnsureTranslateTransform(control, 0, 0);

        var xTask = animationStateService.AnimateTransformAsync(
            transform,
            TransformAxis.X,
            transform.X,
            targetX,
            durationMs,
            EaseOutBack,
            cancellationToken);

        var yTask = animationStateService.AnimateTransformAsync(
            transform,
            TransformAxis.Y,
            transform.Y,
            targetY,
            durationMs,
            EaseOutBack,
            cancellationToken);

        await Task.WhenAll(xTask, yTask);
    }

    public async Task AnimateStaggeredAsync<T>(IEnumerable<T> items, Func<T, int, Task> animationFunc, int staggerDelayMs, CancellationToken cancellationToken)
    {
        var itemsList = items.ToList();
        var tasks = new List<Task>();

        for (int i = 0; i < itemsList.Count; i++)
        {
            if (cancellationToken.IsCancellationRequested)
                break;

            var index = i;
            var item = itemsList[i];

            tasks.Add(Task.Run(async () =>
            {
                await Task.Delay(index * staggerDelayMs, cancellationToken);
                await animationFunc(item, index);
            }, cancellationToken));
        }

        await Task.WhenAll(tasks);
    }

    private (TransformAxis axis, double from, double to) GetSlideParameters(SlideDirection direction, bool isSlideIn, AnimationConfig config)
    {
        return direction switch
        {
            SlideDirection.Up => (TransformAxis.Y, isSlideIn ? config.SlideUpDistance : 0, isSlideIn ? 0 : -config.SlideUpDistance),
            SlideDirection.Down => (TransformAxis.Y, isSlideIn ? -config.SlideUpDistance : 0, isSlideIn ? 0 : config.SlideUpDistance),
            SlideDirection.Left => (TransformAxis.X, isSlideIn ? config.SlideLeftDistance : 0, isSlideIn ? 0 : -config.SlideLeftDistance),
            SlideDirection.Right => (TransformAxis.X, isSlideIn ? -config.SlideLeftDistance : 0, isSlideIn ? 0 : config.SlideLeftDistance),
            _ => throw new ArgumentException($"Unknown slide direction: {direction}", nameof(direction))
        };
    }

    private static double EaseOutBack(double t)
    {
        const double c1 = 1.70158;
        const double c3 = c1 + 1;
        return 1 + c3 * Math.Pow(t - 1, 3) + c1 * Math.Pow(t - 1, 2);
    }

    private static double EaseInQuad(double t)
    {
        return t * t;
    }
}
