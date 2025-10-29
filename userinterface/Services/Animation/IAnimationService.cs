using Avalonia.Controls;
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace userinterface.Services.Animation;

public interface IAnimationService
{
    Task SlideInAsync(Control control, SlideDirection direction, CancellationToken cancellationToken);

    Task SlideOutAsync(Control control, SlideDirection direction, CancellationToken cancellationToken);

    Task FadeInAsync(Control control, int durationMs, CancellationToken cancellationToken);

    Task FadeOutAsync(Control control, int durationMs, CancellationToken cancellationToken);

    Task SlideAndFadeInAsync(Control control, SlideDirection direction, CancellationToken cancellationToken);

    Task SlideAndFadeOutAsync(Control control, SlideDirection direction, CancellationToken cancellationToken);

    Task ExpandAsync(Control control, double targetHeight, int durationMs, CancellationToken cancellationToken);

    Task CollapseAsync(Control control, int durationMs, CancellationToken cancellationToken);

    Task AnimateToPositionAsync(Control control, double targetX, double targetY, int durationMs, CancellationToken cancellationToken);

    Task AnimateStaggeredAsync<T>(IEnumerable<T> items, Func<T, int, Task> animationFunc, int staggerDelayMs, CancellationToken cancellationToken);
}

public enum SlideDirection
{
    Up,
    Down,
    Left,
    Right
}
