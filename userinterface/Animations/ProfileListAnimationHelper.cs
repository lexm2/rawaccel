using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using System;
using System.Collections.Generic;
using System.Linq;
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
        var targetPosition = CalculatePositionForIndex(position + 1);
        await AnimateItemToPositionAsync(profileIndex, targetPosition, staggerIndex);
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
                var targetPosition = CalculatePositionForIndex(0, false);
                animationTasks.Add(AnimateItemCollapseAsync(i, targetPosition, i).AsTask());
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
        var targetMargin = new Thickness(8, targetY, 8, 0);

        if (addProfileButton.Margin == targetMargin) return;

        var animation = GetOrCreateMoveAnimation();
        animation.Children.Clear();
        animation.Children.Add(new KeyFrame
        {
            Cue = new Cue(0d),
            Setters =
            {
                new Setter { Property = Layoutable.MarginProperty, Value = addProfileButton.Margin }
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

        await animation.RunAsync(addProfileButton);
        addProfileButton.Margin = targetMargin;
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
