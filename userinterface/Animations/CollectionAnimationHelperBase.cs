using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Styling;
using System;
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
                animationTasks.Add(AnimateItemToPositionAsync(index, targetPosition, staggerIndex).AsTask());
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
                animationTasks.Add(AnimateItemCollapseAsync(i, targetPosition, i).AsTask());
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
