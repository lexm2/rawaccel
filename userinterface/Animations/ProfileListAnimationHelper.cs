using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Styling;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using userinterface.Services;
using userspace_backend.Logging;

namespace userinterface.Animations;

public class ProfileListAnimationHelper : CollectionAnimationHelperBase<Border>
{
    private readonly List<Border> profiles;
    private readonly Panel profileContainer;
    private readonly Border addProfileButton;
    private readonly Dictionary<int, Button> deleteButtonCache = new();

    public ProfileListAnimationHelper(
        List<Border> profiles,
        Panel profileContainer,
        Border addProfileButton,
        IFrameTimerService frameTimer,
        IAnimationStateService animationStateService,
        ILoggingService? loggingService = null)
        : base(frameTimer, animationStateService, loggingService)
    {
        this.profiles = profiles ?? throw new ArgumentNullException(nameof(profiles));
        this.profileContainer = profileContainer ?? throw new ArgumentNullException(nameof(profileContainer));
        this.addProfileButton = addProfileButton ?? throw new ArgumentNullException(nameof(addProfileButton));
    }

    public override int GetItemCount() => profiles.Count;

    public override Control? GetContainerAtIndex(int index)
    {
        if (index < 0 || index >= profiles.Count)
            return null;
        return profiles[index];
    }

    protected override string GetAnimationContext() => "ProfileList";

    protected override PositioningMode GetPositioningMode() => PositioningMode.TransformOperations;

    public double CalculatePositionForIndex(int index, bool includeAddButton)
    {
        var adjustedIndex = includeAddButton ? index + 1 : index;
        return base.CalculatePositionForIndex(adjustedIndex);
    }

    public override void UpdateInteractionState(bool enabled)
    {
        UpdateDeleteButtonStates(enabled);
    }

    private void UpdateDeleteButtonStates(bool enabled = true)
    {
        var shouldEnable = enabled && !animationStateService.AreAnimationsActive;
        for (int i = 0; i < profiles.Count; i++)
        {
            if (deleteButtonCache.TryGetValue(i, out var cachedButton))
            {
                cachedButton.IsEnabled = shouldEnable;
                continue;
            }

            if (profiles[i].Child is Grid grid)
            {
                var deleteButton = grid.Children.OfType<Button>().FirstOrDefault(b => b.Classes.Contains("DeleteButton"));
                if (deleteButton != null)
                {
                    deleteButton.IsEnabled = shouldEnable;
                    deleteButtonCache[i] = deleteButton;
                }
            }
        }
    }

    public async ValueTask AnimateProfileToPositionAsync(int profileIndex, int position, int staggerIndex = 0)
    {
        var targetY = CalculatePositionForIndex(position + 1);
        await AnimateItemToTransformPositionAsync(profileIndex, targetY, staggerIndex);
    }

    public async ValueTask AnimateAllProfilesToCorrectPositionsAsync(int focusIndex = -1)
    {
        await AnimateAllItemsAsync(focusIndex);
    }

    public async ValueTask ExpandProfileAnimationAsync()
    {
        var animationTasks = new List<Task>();

        if (addProfileButton != null)
        {
            animationTasks.Add(AnimateAddProfileButtonToPositionAsync(0, true));
        }

        animationTasks.Add(AnimateAllProfilesToCorrectPositionsAsync(-1).AsTask());

        if (animationTasks.Count > 0)
        {
            await Task.WhenAll(animationTasks);
        }
    }

    public async ValueTask CollapseProfileAnimationAsync()
    {
        if (profiles.Count == 0) return;

        frameTimer.StartMonitoring("CollapseProfileAnimation");
        var animationTasks = new List<Task>();
        try
        {
            animationStateService.CancelAllAnimations(GetAnimationContext());
            animationStateService.SetAnimationsActive(true);
            UpdateDeleteButtonStates(false);

            if (addProfileButton != null)
            {
                animationTasks.Add(AnimateAddProfileButtonToPositionAsync(0, false));
            }

            for (int i = 0; i < profiles.Count; i++)
            {
                var targetY = CalculatePositionForIndex(0, false);
                animationTasks.Add(AnimateItemToTransformPositionAsync(i, targetY, i).AsTask());
            }

            if (animationTasks.Count > 0)
            {
                try
                {
                    await Task.WhenAll(animationTasks);
                }
                finally
                {
                    animationStateService.SetAnimationsActive(false);
                    UpdateDeleteButtonStates(true);
                }
            }
        }
        finally
        {
            frameTimer.StopMonitoring("CollapseProfileAnimation");
        }
    }

    private async Task AnimateAddProfileButtonToPositionAsync(int targetPosition, bool includeAddButton)
    {
        if (addProfileButton == null) return;

        var targetY = CalculatePositionForIndex(targetPosition, includeAddButton);

        var transform = addProfileButton.RenderTransform as TranslateTransform;
        if (transform == null)
        {
            transform = new TranslateTransform();
            addProfileButton.RenderTransform = transform;
        }

        var currentY = transform.Y;

        if (Math.Abs(currentY - targetY) < 0.1) return;

        var cancellationToken = await animationStateService.RegisterAnimationAsync(GetAnimationContext(), -1);
        Interlocked.Increment(ref activeAnimationCount);

        try
        {
            if (cancellationToken.IsCancellationRequested) return;

            var animation = new Avalonia.Animation.Animation
            {
                Duration = TimeSpan.FromMilliseconds(300),
                FillMode = FillMode.Forward,
                Easing = Easing.Parse("0.25,0.1,0.25,1")
            };

            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(0d),
                Setters =
                {
                    new Setter { Property = TranslateTransform.YProperty, Value = currentY }
                }
            });
            animation.Children.Add(new KeyFrame
            {
                Cue = new Cue(1d),
                Setters =
                {
                    new Setter { Property = TranslateTransform.YProperty, Value = targetY }
                }
            });

            await animation.RunAsync(addProfileButton, cancellationToken);

            if (!cancellationToken.IsCancellationRequested)
            {
                transform.Y = targetY;
            }
        }
        catch (OperationCanceledException)
        {
            transform.Y = targetY;
        }
        catch (Exception ex)
        {
            loggingService?.LogError(LogSource.UI, ex, "Add profile button animation error");
        }
        finally
        {
            animationStateService.UnregisterAnimation(GetAnimationContext(), -1);
            Interlocked.Decrement(ref activeAnimationCount);
        }
    }

    public async Task AnimateProfileToPosition(int profileIndex, int position, int staggerIndex = 0)
    {
        await AnimateProfileToPositionAsync(profileIndex, position, staggerIndex);
    }

    public async Task AnimateAllProfilesToCorrectPositions(int focusIndex = -1)
    {
        await AnimateAllProfilesToCorrectPositionsAsync(focusIndex);
    }

    public async Task ExpandProfileAnimation()
    {
        await ExpandProfileAnimationAsync();
    }

    public async Task CollapseProfileAnimation()
    {
        await CollapseProfileAnimationAsync();
    }

    public override void Dispose()
    {
        deleteButtonCache.Clear();
        base.Dispose();
    }
}
