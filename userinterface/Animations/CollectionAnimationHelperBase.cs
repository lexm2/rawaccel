using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Styling;
using System;
using System.Text.RegularExpressions;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using userinterface.Services;
using userinterface.Utilities;
using userspace_backend.Logging;

namespace userinterface.Animations;

public abstract class CollectionAnimationHelperBase<TContainer> : ICollectionAnimationHelper where TContainer : Control
{
    protected readonly IAnimationStateService animationStateService;
    protected readonly IFrameTimerService frameTimer;
    protected readonly ILoggingService? loggingService;

    private bool disposed = false;

    private readonly ObjectPool<Animation> animationPool = new(() => new Animation());
    private readonly ObjectPool<List<Task>> taskListPool = new(() => new List<Task>());

    private readonly Dictionary<int, double> positionCache = new();
    private readonly Dictionary<string, Animation> animationTemplateCache = new();

    private Animation? cachedItemMoveAnimation;
    private Animation? cachedItemCollapseAnimation;

    protected volatile int activeAnimationCount = 0;

    protected CollectionAnimationHelperBase(
        IFrameTimerService frameTimer,
        IAnimationStateService animationStateService,
        ILoggingService? loggingService = null)
    {
        this.frameTimer = frameTimer ?? throw new ArgumentNullException(nameof(frameTimer));
        this.animationStateService = animationStateService ?? throw new ArgumentNullException(nameof(animationStateService));
        this.loggingService = loggingService;
    }

    public bool AreAnimationsActive
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get => animationStateService.AreAnimationsActive;
    }

    public abstract int GetItemCount();

    public abstract Control? GetContainerAtIndex(int index);

    protected abstract string GetAnimationContext();

    protected virtual double GetItemHeight() => animationStateService.Config.ProfileHeight;

    protected virtual double GetItemSpacing() => animationStateService.Config.ProfileSpacing;

    protected virtual double GetFirstIndexOffset() => animationStateService.Config.FirstIndexOffset;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public virtual double CalculatePositionForIndex(int index)
    {
        if (positionCache.TryGetValue(index, out var cachedPosition))
            return cachedPosition;

        var position = index == 0 ? 0 : (index * (GetItemHeight() + GetItemSpacing())) + GetFirstIndexOffset();
        positionCache[index] = position;
        return position;
    }

    protected void InvalidatePositionCache()
    {
        positionCache.Clear();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    protected int CalculateStaggerIndex(int itemIndex, int focusIndex, int maxDistance = 3)
    {
        if (focusIndex >= 0 && itemIndex != focusIndex)
            return Math.Min(Math.Abs(itemIndex - focusIndex), maxDistance);
        return Math.Min(itemIndex, maxDistance);
    }

    protected Animation GetOrCreateMoveAnimation()
    {
        if (cachedItemMoveAnimation == null)
        {
            cachedItemMoveAnimation = new Animation
            {
                Duration = TimeSpan.FromMilliseconds(300),
                FillMode = FillMode.Forward,
                Easing = Easing.Parse("0.25,0.1,0.25,1")
            };
        }
        return cachedItemMoveAnimation;
    }

    protected Animation GetOrCreateCollapseAnimation()
    {
        if (cachedItemCollapseAnimation == null)
        {
            cachedItemCollapseAnimation = new Animation
            {
                Duration = TimeSpan.FromMilliseconds(300),
                FillMode = FillMode.Forward,
                Easing = Easing.Parse("0.25,0.1,0.25,1")
            };
        }
        return cachedItemCollapseAnimation;
    }

    protected virtual Thickness CalculateTargetMargin(double position)
    {
        return new Thickness(8, position, 8, 0);
    }

    public virtual void UpdateAllZIndexes()
    {
        for (int i = 0; i < GetItemCount(); i++)
        {
            var container = GetContainerAtIndex(i);
            if (container != null)
            {
                container.ZIndex = i;
            }
        }
    }

    public abstract void UpdateInteractionState(bool enabled);

    public virtual void CancelAllAnimations()
    {
        animationStateService.CancelAllAnimations(GetAnimationContext());
        Interlocked.Exchange(ref activeAnimationCount, 0);
        UpdateInteractionState(true);
    }

    public virtual async ValueTask AnimateItemToPositionAsync(int itemIndex, double position, int staggerIndex = 0)
    {
        var container = GetContainerAtIndex(itemIndex);
        if (container == null) return;

        var targetMargin = CalculateTargetMargin(position);
        if (container.Margin == targetMargin && container.ZIndex == (int)position)
        {
            return;
        }

        var cancellationToken = await animationStateService.RegisterAnimationAsync(GetAnimationContext(), itemIndex);
        Interlocked.Increment(ref activeAnimationCount);

        try
        {
            if (staggerIndex > 0 && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(staggerIndex * animationStateService.Config.StaggerDelayMs, cancellationToken);
            }

            if (cancellationToken.IsCancellationRequested) return;

            var animation = GetOrCreateMoveAnimation();
            animation.Children.Clear();
            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(0d),
                Setters =
                {
                    new Setter { Property = Control.OpacityProperty, Value = 1.0 }
                }
            });
            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(1d),
                Setters =
                {
                    new Setter { Property = Layoutable.MarginProperty, Value = targetMargin },
                    new Setter { Property = Control.OpacityProperty, Value = 1.0 }
                }
            });

            await animation.RunAsync(container, cancellationToken);

            if (!cancellationToken.IsCancellationRequested)
            {
                container.Margin = targetMargin;
                container.ZIndex = (int)position;
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            loggingService?.LogError(LogSource.UI, ex, "Animation error for item {ItemIndex}", itemIndex);
        }
        finally
        {
            animationStateService.UnregisterAnimation(GetAnimationContext(), itemIndex);
            var remainingCount = Interlocked.Decrement(ref activeAnimationCount);
            loggingService?.LogDebug(LogSource.UI, "[ANIMATION] Cleaned up animation for item {ItemIndex}, remaining: {RemainingCount}", itemIndex, remainingCount);
        }
    }

    public virtual async ValueTask AnimateAllItemsAsync(int focusIndex = -1)
    {
        frameTimer.StartMonitoring($"AnimateAllItems focusIndex={focusIndex}");
        var animationTasks = taskListPool.Get();
        try
        {
            animationStateService.CancelAllAnimations(GetAnimationContext());

            var animationsToRun = new List<(int index, int staggerIndex)>();
            var itemCount = GetItemCount();

            for (int i = 0; i < itemCount; i++)
            {
                var container = GetContainerAtIndex(i);
                if (container == null) continue;

                var targetPosition = CalculatePositionForIndex(i);
                var targetMargin = CalculateTargetMargin(targetPosition);

                if (container.Margin == targetMargin)
                {
                    container.ZIndex = i;
                    continue;
                }

                int staggerIndex = CalculateStaggerIndex(i, focusIndex);
                animationsToRun.Add((i, staggerIndex));
            }

            var deduplicatedAnimations = animationsToRun
                .GroupBy(a => a.index)
                .Select(g => g.Last())
                .OrderBy(a => a.staggerIndex)
                .ToList();

            foreach (var (index, staggerIndex) in deduplicatedAnimations)
            {
                var targetPosition = CalculatePositionForIndex(index);
                if (GetPositioningMode() == PositioningMode.TransformOperations)
                {
                    animationTasks.Add(AnimateItemToTransformPositionAsync(index, targetPosition, staggerIndex).AsTask());
                }
                else
                {
                    animationTasks.Add(AnimateItemToPositionAsync(index, targetPosition, staggerIndex).AsTask());
                }
            }

            if (animationTasks.Count > 0)
            {
                animationStateService.SetAnimationsActive(true);
                UpdateInteractionState(false);

                try
                {
                    await Task.WhenAll(animationTasks);
                }
                catch (Exception ex)
                {
                    loggingService?.LogError(LogSource.UI, ex, "Batch animation error");
                }
                finally
                {
                    animationStateService.SetAnimationsActive(false);
                    UpdateInteractionState(true);
                }
            }
        }
        finally
        {
            frameTimer.StopMonitoring($"AnimateAllItems focusIndex={focusIndex}");
            animationTasks.Clear();
            taskListPool.Return(animationTasks);
        }
    }

    protected virtual async ValueTask AnimateItemCollapseAsync(int itemIndex, double targetPosition, int staggerIndex = 0)
    {
        var container = GetContainerAtIndex(itemIndex);
        if (container == null) return;

        var cancellationToken = await animationStateService.RegisterAnimationAsync(GetAnimationContext(), itemIndex);
        Interlocked.Increment(ref activeAnimationCount);

        try
        {
            if (staggerIndex > 0 && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(staggerIndex * animationStateService.Config.StaggerDelayMs, cancellationToken);
            }

            if (cancellationToken.IsCancellationRequested) return;

            var targetMargin = CalculateTargetMargin(targetPosition);
            var animation = GetOrCreateCollapseAnimation();
            animation.Children.Clear();
            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(0d),
                Setters =
                {
                    new Setter { Property = Layoutable.MarginProperty, Value = container.Margin }
                }
            });
            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(1d),
                Setters =
                {
                    new Setter { Property = Layoutable.MarginProperty, Value = targetMargin }
                }
            });

            await animation.RunAsync(container, cancellationToken);

            if (!cancellationToken.IsCancellationRequested)
            {
                container.Margin = targetMargin;
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            loggingService?.LogError(LogSource.UI, ex, "Collapse animation error for item {ItemIndex}", itemIndex);
        }
        finally
        {
            animationStateService.UnregisterAnimation(GetAnimationContext(), itemIndex);
            Interlocked.Decrement(ref activeAnimationCount);
        }
    }

    public virtual async ValueTask ExpandAsync()
    {
        await AnimateAllItemsAsync(-1);
    }

    public virtual async ValueTask CollapseAsync()
    {
        var itemCount = GetItemCount();
        if (itemCount == 0) return;

        frameTimer.StartMonitoring("CollapseAnimation");
        var animationTasks = taskListPool.Get();
        try
        {
            animationStateService.CancelAllAnimations(GetAnimationContext());
            animationStateService.SetAnimationsActive(true);
            UpdateInteractionState(false);

            var targetPosition = CalculatePositionForIndex(0);

            for (int i = 0; i < itemCount; i++)
            {
                if (GetPositioningMode() == PositioningMode.TransformOperations)
                {
                    animationTasks.Add(AnimateItemToTransformPositionAsync(i, targetPosition, i).AsTask());
                }
                else
                {
                    animationTasks.Add(AnimateItemCollapseAsync(i, targetPosition, i).AsTask());
                }
            }

            if (animationTasks.Count > 0)
            {
                try
                {
                    await Task.WhenAll(animationTasks);
                }
                catch (Exception ex)
                {
                    loggingService?.LogError(LogSource.UI, ex, "Collapse batch animation error");
                }
                finally
                {
                    animationStateService.SetAnimationsActive(false);
                    UpdateInteractionState(true);
                }
            }
        }
        finally
        {
            frameTimer.StopMonitoring("CollapseAnimation");
            animationTasks.Clear();
            taskListPool.Return(animationTasks);
        }
    }

    protected virtual bool SupportsVirtualization => false;

    protected virtual double GetSlideDistance(SlideDirection direction)
    {
        return direction switch
        {
            SlideDirection.Left or SlideDirection.Right => animationStateService.Config.SlideLeftDistance,
            SlideDirection.Up or SlideDirection.Down => animationStateService.Config.SlideUpDistance,
            _ => animationStateService.Config.SlideUpDistance
        };
    }

    protected virtual async ValueTask AnimateTransformAsync(int itemIndex, double targetX, double targetY, double targetOpacity, int durationMs, int staggerIndex = 0)
    {
        var container = GetContainerAtIndex(itemIndex);
        if (container == null)
        {
            if (SupportsVirtualization)
            {
                loggingService?.LogDebug(LogSource.UI, "Container at index {Index} not found (virtualized), skipping animation", itemIndex);
                return;
            }
            loggingService?.LogWarning(LogSource.UI, "Container at index {Index} not found", itemIndex);
            return;
        }

        var cancellationToken = await animationStateService.RegisterAnimationAsync(GetAnimationContext(), itemIndex);
        Interlocked.Increment(ref activeAnimationCount);

        try
        {
            if (staggerIndex > 0 && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(staggerIndex * animationStateService.Config.StaggerDelayMs, cancellationToken);
            }

            if (cancellationToken.IsCancellationRequested) return;

            var transform = container.RenderTransform as TranslateTransform ?? new TranslateTransform();
            if (container.RenderTransform == null)
            {
                container.RenderTransform = transform;
            }

            var startX = transform.X;
            var startY = transform.Y;
            var startOpacity = container.Opacity;

            var animation = new Animation
            {
                Duration = TimeSpan.FromMilliseconds(durationMs),
                FillMode = FillMode.Forward,
                Easing = Easing.Parse("0.25,0.1,0.25,1")
            };

            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(0d),
                Setters =
                {
                    new Setter { Property = TranslateTransform.XProperty, Value = startX },
                    new Setter { Property = TranslateTransform.YProperty, Value = startY },
                    new Setter { Property = Control.OpacityProperty, Value = startOpacity }
                }
            });

            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(1d),
                Setters =
                {
                    new Setter { Property = TranslateTransform.XProperty, Value = targetX },
                    new Setter { Property = TranslateTransform.YProperty, Value = targetY },
                    new Setter { Property = Control.OpacityProperty, Value = targetOpacity }
                }
            });

            await animation.RunAsync(transform, cancellationToken);

            if (!cancellationToken.IsCancellationRequested)
            {
                transform.X = targetX;
                transform.Y = targetY;
                container.Opacity = targetOpacity;
            }
        }
        catch (OperationCanceledException)
        {
            if (container != null)
            {
                var transform = container.RenderTransform as TranslateTransform;
                if (transform != null)
                {
                    transform.X = targetX;
                    transform.Y = targetY;
                }
                container.Opacity = targetOpacity;
            }
        }
        catch (Exception ex)
        {
            loggingService?.LogError(LogSource.UI, ex, "Transform animation error for item {ItemIndex}", itemIndex);
        }
        finally
        {
            animationStateService.UnregisterAnimation(GetAnimationContext(), itemIndex);
            var remainingCount = Interlocked.Decrement(ref activeAnimationCount);
            loggingService?.LogDebug(LogSource.UI, "[ANIMATION] Cleaned up transform animation for item {ItemIndex}, remaining: {RemainingCount}", itemIndex, remainingCount);
        }
    }

    protected virtual async ValueTask AnimateOpacityAsync(int itemIndex, double targetOpacity, int durationMs, int staggerIndex = 0)
    {
        var container = GetContainerAtIndex(itemIndex);
        if (container == null)
        {
            if (SupportsVirtualization)
            {
                loggingService?.LogDebug(LogSource.UI, "Container at index {Index} not found (virtualized), skipping opacity animation", itemIndex);
                return;
            }
            loggingService?.LogWarning(LogSource.UI, "Container at index {Index} not found", itemIndex);
            return;
        }

        var cancellationToken = await animationStateService.RegisterAnimationAsync(GetAnimationContext(), itemIndex);
        Interlocked.Increment(ref activeAnimationCount);

        try
        {
            if (staggerIndex > 0 && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(staggerIndex * animationStateService.Config.StaggerDelayMs, cancellationToken);
            }

            if (cancellationToken.IsCancellationRequested) return;

            var startOpacity = container.Opacity;
            var animation = new Animation
            {
                Duration = TimeSpan.FromMilliseconds(durationMs),
                FillMode = FillMode.Forward,
                Easing = new LinearEasing()
            };

            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(0d),
                Setters =
                {
                    new Setter { Property = Control.OpacityProperty, Value = startOpacity }
                }
            });

            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(1d),
                Setters =
                {
                    new Setter { Property = Control.OpacityProperty, Value = targetOpacity }
                }
            });

            await animation.RunAsync(container, cancellationToken);

            if (!cancellationToken.IsCancellationRequested)
            {
                container.Opacity = targetOpacity;
            }
        }
        catch (OperationCanceledException)
        {
            if (container != null)
            {
                container.Opacity = targetOpacity;
            }
        }
        catch (Exception ex)
        {
            loggingService?.LogError(LogSource.UI, ex, "Opacity animation error for item {ItemIndex}", itemIndex);
        }
        finally
        {
            animationStateService.UnregisterAnimation(GetAnimationContext(), itemIndex);
            Interlocked.Decrement(ref activeAnimationCount);
        }
    }

    protected virtual async ValueTask AnimateSlideAsync(int itemIndex, SlideDirection direction, int durationMs, int staggerIndex = 0)
    {
        var distance = GetSlideDistance(direction);
        var (targetX, targetY) = direction switch
        {
            SlideDirection.Up => (0.0, -distance),
            SlideDirection.Down => (0.0, distance),
            SlideDirection.Left => (-distance, 0.0),
            SlideDirection.Right => (distance, 0.0),
            _ => (0.0, -distance)
        };

        await AnimateTransformAsync(itemIndex, targetX, targetY, 1.0, durationMs, staggerIndex);
    }

    protected virtual async ValueTask AnimateSlideAndFadeAsync(int itemIndex, SlideDirection direction, double targetOpacity, int durationMs, int staggerIndex = 0)
    {
        var distance = GetSlideDistance(direction);
        var (targetX, targetY) = direction switch
        {
            SlideDirection.Up => (0.0, -distance),
            SlideDirection.Down => (0.0, distance),
            SlideDirection.Left => (-distance, 0.0),
            SlideDirection.Right => (distance, 0.0),
            _ => (0.0, -distance)
        };

        await AnimateTransformAsync(itemIndex, targetX, targetY, targetOpacity, durationMs, staggerIndex);
    }

    protected virtual PositioningMode GetPositioningMode() => PositioningMode.Margin;

    protected double ExtractYFromRenderTransform(Control container)
    {
        if (container.RenderTransform is ITransform transform)
        {
            var transformString = transform.ToString();
            if (!string.IsNullOrEmpty(transformString))
            {
                var match = Regex.Match(transformString, @"translate\(0px,\s*(-?\d+(?:\.\d+)?)px\)");
                if (match.Success && double.TryParse(match.Groups[1].Value, out var y))
                {
                    return y;
                }
            }
        }
        return 0.0;
    }

    protected virtual async ValueTask AnimateItemToTransformPositionAsync(int itemIndex, double targetY, int staggerIndex = 0)
    {
        var container = GetContainerAtIndex(itemIndex);
        if (container == null)
        {
            if (SupportsVirtualization)
            {
                loggingService?.LogDebug(LogSource.UI, "Container at index {Index} not found (virtualized), skipping transform position animation", itemIndex);
                return;
            }
            loggingService?.LogWarning(LogSource.UI, "Container at index {Index} not found", itemIndex);
            return;
        }

        var currentY = ExtractYFromRenderTransform(container);
        if (Math.Abs(currentY - targetY) < 0.1)
        {
            container.ZIndex = (int)(targetY / (GetItemHeight() + GetItemSpacing()));
            return;
        }

        var cancellationToken = await animationStateService.RegisterAnimationAsync(GetAnimationContext(), itemIndex);
        Interlocked.Increment(ref activeAnimationCount);

        try
        {
            if (staggerIndex > 0 && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(staggerIndex * animationStateService.Config.StaggerDelayMs, cancellationToken);
            }

            if (cancellationToken.IsCancellationRequested) return;

            var animation = GetOrCreateMoveAnimation();
            animation.Children.Clear();
            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(0d),
                Setters =
                {
                    new Setter
                    {
                        Property = Control.RenderTransformProperty,
                        Value = TransformOperations.Parse($"translate(0px, {currentY}px)")
                    }
                }
            });
            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(1d),
                Setters =
                {
                    new Setter
                    {
                        Property = Control.RenderTransformProperty,
                        Value = TransformOperations.Parse($"translate(0px, {targetY}px)")
                    }
                }
            });

            await animation.RunAsync(container, cancellationToken);

            if (!cancellationToken.IsCancellationRequested)
            {
                container.RenderTransform = TransformOperations.Parse($"translate(0px, {targetY}px)");
                container.ZIndex = (int)(targetY / (GetItemHeight() + GetItemSpacing()));
            }
        }
        catch (OperationCanceledException)
        {
            container.RenderTransform = TransformOperations.Parse($"translate(0px, {targetY}px)");
            container.ZIndex = (int)(targetY / (GetItemHeight() + GetItemSpacing()));
        }
        catch (Exception ex)
        {
            loggingService?.LogError(LogSource.UI, ex, "Transform position animation error for item {ItemIndex}", itemIndex);
        }
        finally
        {
            animationStateService.UnregisterAnimation(GetAnimationContext(), itemIndex);
            var remainingCount = Interlocked.Decrement(ref activeAnimationCount);
            loggingService?.LogDebug(LogSource.UI, "[ANIMATION] Cleaned up transform position animation for item {ItemIndex}, remaining: {RemainingCount}", itemIndex, remainingCount);
        }
    }

    public virtual void Dispose()
    {
        if (!disposed)
        {
            disposed = true;

            CancelAllAnimations();

            animationPool?.Dispose();
            taskListPool?.Dispose();

            positionCache.Clear();
            animationTemplateCache.Clear();

            cachedItemMoveAnimation = null;
            cachedItemCollapseAnimation = null;
        }
    }
}

public enum SlideDirection
{
    Up,
    Down,
    Left,
    Right
}

public enum PositioningMode
{
    Margin,
    TransformOperations
}
