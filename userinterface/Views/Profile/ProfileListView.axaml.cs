using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Styling;
using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;
using userinterface.Animations;
using userinterface.Services;
using userinterface.Services.Animation;
using userinterface.ViewModels.Profile;
using userspace_backend;
using BE = userspace_backend.Model;

namespace userinterface.Views.Profile;

public partial class ProfileListView : UserControl, INotifyPropertyChanged
{
    private readonly List<Border> profileItems = [];
    private Border? addProfileButton;
    private Panel? profileContainer;
    private readonly BE.IProfilesModel profilesModel;
    private BE.IProfileModel? selectedProfile;

    private int GetProfileCount() => profileItems.Count;
    private readonly IAnimationStateService animationStateService;
    private readonly IAnimationService animationService;
    private readonly IFrameTimerService frameTimer;
    private readonly userspace_backend.Logging.ILoggingService? loggingService;
    private ProfileListAnimationHelper? animationHelper;

    public new event PropertyChangedEventHandler? PropertyChanged;
    private readonly IModalService modalService;
    private readonly ILocalizationService localizationService;
    private TextBlock? addProfileTextBlock;


    public ProfileListView()
    {
        var backEnd = App.Services?.GetRequiredService<IBackEnd>() ?? throw new InvalidOperationException("BackEnd service not available");
        modalService = App.Services?.GetRequiredService<IModalService>() ?? throw new InvalidOperationException("ModalService not available");
        localizationService = App.Services?.GetRequiredService<ILocalizationService>() ?? throw new InvalidOperationException("LocalizationService not available");
        animationStateService = App.Services?.GetRequiredService<IAnimationStateService>() ?? throw new InvalidOperationException("AnimationStateService not available");
        animationService = App.Services?.GetRequiredService<IAnimationService>() ?? throw new InvalidOperationException("AnimationService not available");
        frameTimer = App.Services?.GetRequiredService<IFrameTimerService>() ?? throw new InvalidOperationException("FrameTimerService not available");
        loggingService = App.Services?.GetService(typeof(userspace_backend.Logging.ILoggingService)) as userspace_backend.Logging.ILoggingService;

        profilesModel = backEnd.Profiles ?? throw new ArgumentNullException(nameof(backEnd.Profiles));
        localizationService.PropertyChanged += OnLocalizationPropertyChanged;
        ((INotifyCollectionChanged)profilesModel.Elements).CollectionChanged += OnProfilesCollectionChanged;

        InitializeComponent();

        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnUnloaded(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (localizationService != null)
        {
            localizationService.PropertyChanged -= OnLocalizationPropertyChanged;
        }

        if (DataContext is ProfileListViewModel viewModel)
        {
            viewModel.PropertyChanged -= OnViewModelPropertyChanged;
        }

        animationHelper?.Dispose();
        animationHelper = null;
    }

    private void OnLoaded(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        profileContainer = this.FindControl<Panel>("ProfileContainer");

        if (DataContext is ProfileListViewModel viewModel)
        {
            viewModel.PropertyChanged += OnViewModelPropertyChanged;
        }

        addProfileButton = CreateAddProfileButton();
        profileContainer?.Children.Add(addProfileButton);

        if (addProfileButton != null && profileContainer != null)
        {
            animationHelper = new ProfileListAnimationHelper(
                profileItems,
                profileContainer,
                addProfileButton,
                frameTimer,
                animationStateService,
                loggingService
            );
        }

        CreateProfilesWithStagger();

        _ = ExpandElements();
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(ProfileListViewModel.SelectedProfile) && sender is ProfileListViewModel viewModel)
        {
            SetSelectedProfile(viewModel.SelectedProfile);
        }
    }

    private void OnLocalizationPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (addProfileTextBlock != null)
        {
            addProfileTextBlock.Text = localizationService?.GetText("ProfileAddNewProfile") ?? "Add New Profile";
        }
    }

    private async void OnProfilesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        await animationStateService.ExecuteWithSemaphoreAsync(async () =>
        {
            switch (e.Action)
            {
                case NotifyCollectionChangedAction.Add:
                    HandleProfilesAdded(e);
                    break;
                case NotifyCollectionChangedAction.Remove:
                    await HandleProfilesRemoved(e);
                    break;
                case NotifyCollectionChangedAction.Replace:
                    await HandleProfilesReplaced(e);
                    break;
                case NotifyCollectionChangedAction.Move:
                    await HandleProfilesMoved(e);
                    break;
                case NotifyCollectionChangedAction.Reset:
                    await HandleProfilesReset();
                    break;
            }
        });
    }

    private void HandleProfilesAdded(NotifyCollectionChangedEventArgs e)
    {
        if (e.NewItems == null) return;

        // Add profiles at their actual positions in the backend collection
        int startIndex = e.NewStartingIndex >= 0 ? e.NewStartingIndex : profilesModel.Elements.Count - e.NewItems.Count;

        for (int i = 0; i < e.NewItems.Count; i++)
        {
            int profileIndex = startIndex + i;
            AddProfileAtPosition(profileIndex);
        }

        RefreshAllProfileNames();

        if (e.NewItems.Count > 0 && DataContext is ProfileListViewModel viewModel)
        {
            int lastAddedIndex = startIndex + e.NewItems.Count - 1;
            if (lastAddedIndex >= 0 && lastAddedIndex < profilesModel.Elements.Count)
            {
                viewModel.SelectedProfile = profilesModel.Elements[lastAddedIndex];
            }
        }
    }

    private async Task HandleProfilesRemoved(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems == null) return;

        int removeIndex = e.OldStartingIndex >= 0 ? e.OldStartingIndex : GetProfileCount() - 1;
        int removeCount = e.OldItems.Count;


        for (int i = 0; i < removeCount && removeIndex >= 0 && removeIndex < GetProfileCount(); i++)
        {
            RemoveProfileAt(removeIndex);
        }

        UpdateAllZIndexes();

        await AnimateAllElementsToPositions(removeIndex);

        if (selectedProfile != null && !profilesModel.Elements.Contains(selectedProfile) && DataContext is ProfileListViewModel viewModel)
        {
            var defaultProfile = profilesModel.Elements.FirstOrDefault(p => p.Name.ModelValue == "Default");
            if (defaultProfile != null)
            {
                viewModel.SelectedProfile = defaultProfile;
            }
            else if (profilesModel.Elements.Count > 0)
            {
                viewModel.SelectedProfile = profilesModel.Elements[0];
            }
            else
            {
                viewModel.SelectedProfile = null;
            }
        }
    }

    private async Task HandleProfilesReplaced(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems == null || e.NewItems == null || e.OldStartingIndex < 0) return;

        int replaceIndex = e.OldStartingIndex;
        int itemCount = Math.Min(e.OldItems.Count, e.NewItems.Count);

        for (int i = 0; i < itemCount && replaceIndex + i < GetProfileCount(); i++)
        {
            int itemIndex = replaceIndex + i;
            if (itemIndex < profileItems.Count && profileItems[itemIndex].Child is Grid grid)
            {
                var textBlock = grid.Children.OfType<TextBlock>().FirstOrDefault();
                if (textBlock != null)
                {
                    textBlock.Text = $"Profile {replaceIndex + i + 1}";
                }
            }
        }

        UpdateAllZIndexes();

        await AnimateAllElementsToPositions(replaceIndex);
    }

    private async Task HandleProfilesMoved(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldStartingIndex < 0 || e.NewStartingIndex < 0) return;

        MoveProfile(e.OldStartingIndex, e.NewStartingIndex);

        UpdateAllZIndexes();

        await AnimateAllElementsToPositions(Math.Min(e.OldStartingIndex, e.NewStartingIndex));
    }

    private Task HandleProfilesReset()
    {
        profileItems.Clear();
        profileContainer?.Children.Clear();

        if (addProfileButton != null)
        {
            profileContainer?.Children.Add(addProfileButton);
        }

        for (int i = 0; i < profilesModel.Elements.Count; i++)
        {
            AddProfileAtPosition(i);
        }

        UpdateAllZIndexes();

        return Task.CompletedTask;
    }

    private void RemoveProfileAt(int index)
    {
        if (index < 0 || index >= profileItems.Count) return;

        var item = profileItems[index];
        profileItems.RemoveAt(index);
        profileContainer?.Children.Remove(item);
    }

    private void MoveProfile(int fromIndex, int toIndex)
    {
        if (fromIndex < 0 || fromIndex >= profileItems.Count ||
            toIndex < 0 || toIndex >= profileItems.Count ||
            fromIndex == toIndex) return;

        var item = profileItems[fromIndex];
        profileItems.RemoveAt(fromIndex);
        profileItems.Insert(toIndex, item);

        profileContainer?.Children.Remove(item);
        int containerIndex = toIndex + (addProfileButton != null ? 1 : 0);
        profileContainer?.Children.Insert(containerIndex, item);
    }

    private void AddProfileAtPosition(int targetIndex)
    {
        if (targetIndex < 0 || targetIndex > GetProfileCount()) return;

        var profileBorder = CreateProfileBorder(null!, targetIndex);

        profileBorder.ZIndex = 1000;
        profileBorder.Opacity = 1.0;

        profileItems.Insert(targetIndex, profileBorder);
        int containerIndex = targetIndex + (addProfileButton != null ? 1 : 0);
        profileContainer?.Children.Insert(containerIndex, profileBorder);

        UpdateAllZIndexes();

        _ = AnimateAllElementsToPositions(targetIndex);
    }

    private Border CreateAddProfileButton()
    {
        // Create the add profile text
        addProfileTextBlock = new TextBlock
        {
            Text = localizationService?.GetText("ProfileAddNewProfile") ?? "Add New Profile",
            VerticalAlignment = VerticalAlignment.Center,
            HorizontalAlignment = HorizontalAlignment.Center,
            FontWeight = FontWeight.Medium
        };

        var border = new Border
        {
            Classes = { "AddProfileButton" },
            Height = animationStateService.Config.ProfileHeight,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(8, 0, 8, animationStateService.Config.ProfileSpacing),
            RenderTransform = TransformOperations.Parse("translate(0px, 0px)"), // Start at collapsed position
            Child = addProfileTextBlock
        };

        border.PointerPressed += (s, e) => OnAddProfileClicked(s!, e);

        return border;
    }

    private Border CreateProfileBorder(IBrush color, int targetIndex)
    {
        var profileName = targetIndex < profilesModel.Elements.Count ? profilesModel.Elements[targetIndex].CurrentNameForDisplay : $"Profile {targetIndex + 1}";
        var isDefaultProfile = targetIndex < profilesModel.Elements.Count && profilesModel.Elements[targetIndex].Name.ModelValue == "Default";

        var profileText = new TextBlock
        {
            Text = profileName,
            VerticalAlignment = VerticalAlignment.Center
        };

        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));

        Grid.SetColumn(profileText, 0);
        grid.Children.Add(profileText);

        if (!isDefaultProfile)
        {
            // Create the delete button with icon using SimpleDeleteButton
            var deleteButton = new Button
            {
                Classes = { "SimpleDeleteButton" },
                VerticalAlignment = VerticalAlignment.Center,
                Content = new PathIcon
                {
                    Data = Application.Current?.FindResource("delete_regular") as StreamGeometry,
                    Width = 12,
                    Height = 12
                }
            };
            deleteButton.Click += OnDeleteButtonClicked;

            grid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            Grid.SetColumn(deleteButton, 1);
            grid.Children.Add(deleteButton);
        }

        var border = new Border
        {
            Classes = { "ProfileItem" },
            Height = animationStateService.Config.ProfileHeight,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(8, 0, 8, animationStateService.Config.ProfileSpacing),
            RenderTransform = TransformOperations.Parse("translate(0px, 0px)"), // Start at collapsed position
            Child = grid,
            Opacity = 1.0,
            ZIndex = targetIndex
        };

        border.PointerPressed += OnProfileBorderClicked;

        return border;
    }

    private void UpdateDeleteButtonStates()
    {
        for (int i = 1; i < profileItems.Count; i++)
        {
            if (profileItems[i].Child is Grid grid)
            {
                var deleteButton = grid.Children.OfType<Button>().FirstOrDefault(b => b.Classes.Contains("DeleteButton"));
                if (deleteButton != null)
                {
                    deleteButton.IsEnabled = !animationStateService.AreAnimationsActive;
                }
            }
        }
    }

    private void OnProfileBorderClicked(object? sender, Avalonia.Input.PointerPressedEventArgs e)
    {
        if (sender is Border border && DataContext is ProfileListViewModel viewModel)
        {
            int profileIndex = profileItems.IndexOf(border);
            if (profileIndex >= 0 && profileIndex < profilesModel.Elements.Count)
            {
                var clickedProfile = profilesModel.Elements[profileIndex];
                viewModel.SelectedProfile = clickedProfile;
            }
        }
    }

    private void OnAddProfileClicked(object sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        // Prevent rapid clicking during active operations
        if (animationStateService.AreAnimationsActive)
        {
            return;
        }

        // Use the ViewModel's TryAddProfile method
        if (DataContext is ProfileListViewModel viewModel)
        {
            viewModel.TryAddProfile();
        }
    }

    private async void OnDeleteButtonClicked(object? sender, RoutedEventArgs e)
    {
        var logger = App.Services?.GetService<userspace_backend.Logging.ILoggingService>();
        logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, "OnDeleteButtonClicked: Delete button clicked");

        // Prevent deletion during animations to avoid bugs
        if (animationStateService.AreAnimationsActive)
        {
            logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, "OnDeleteButtonClicked: Animations active, returning");
            return;
        }

        // Find which profile this delete button belongs to
        if (sender is Button deleteButton &&
            deleteButton.Parent is Grid grid &&
            grid.Parent is Border border)
        {
            var profileIndex = profileItems.IndexOf(border);
            logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, $"OnDeleteButtonClicked: Profile index = {profileIndex}, Total profiles = {profilesModel.Elements.Count}");

            if (profileIndex >= 0 && profileIndex < profilesModel.Elements.Count)
            {
                var profileToDelete = profilesModel.Elements[profileIndex];
                logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, $"OnDeleteButtonClicked: Profile to delete = '{profileToDelete.Name.ModelValue}'");

                // Show confirmation modal
                logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, "OnDeleteButtonClicked: Calling ShowConfirmationAsync");
                var confirmed = await modalService.ShowConfirmationAsync(
                    "ProfileDeleteTitle",
                    "ProfileDeleteMessage",
                    "ProfileDeleteConfirm",
                    "ModalCancel");

                logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, $"OnDeleteButtonClicked: Modal result = {confirmed}");

                if (confirmed)
                {
                    logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, "OnDeleteButtonClicked: User confirmed, removing profile");
                    profilesModel.TryRemoveElement(profileToDelete);
                }
                else
                {
                    logger?.LogInformation(userspace_backend.Logging.LogSource.Modal, "OnDeleteButtonClicked: User cancelled");
                }
            }
            else
            {
                logger?.LogError(userspace_backend.Logging.LogSource.Modal, $"OnDeleteButtonClicked: Invalid profile index {profileIndex}");
            }
        }
        else
        {
            logger?.LogError(userspace_backend.Logging.LogSource.Modal, "OnDeleteButtonClicked: Could not find parent elements");
        }
    }

    private void UpdateAllZIndexes()
    {
        if (addProfileButton != null)
        {
            addProfileButton.ZIndex = 0;
        }

        for (int i = 0; i < profileItems.Count; i++)
        {
            profileItems[i].ZIndex = i + 1;
        }
    }

    private void CreateProfilesWithStagger()
    {
        for (int i = 0; i < profilesModel.Elements.Count; i++)
        {
            var profileBorder = CreateProfileBorder(null!, i);
            profileBorder.ZIndex = 1000;
            profileBorder.Opacity = 1.0;

            profileItems.Add(profileBorder);
            int containerIndex = i + (addProfileButton != null ? 1 : 0);
            profileContainer?.Children.Insert(containerIndex, profileBorder);
        }

        UpdateAllZIndexes();
        RefreshAllProfileNames();
        UpdateDeleteButtonStates();
    }


    private async Task AnimateAllElementsToPositions(int focusIndex = -1)
    {
        if (animationHelper == null) return;

        await animationHelper.AnimateAllProfilesToCorrectPositionsAsync(focusIndex);

        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(AreAnimationsActive)));
    }

    private void SetSelectedProfile(BE.IProfileModel? profile)
    {
        if (selectedProfile == profile) return;

        for (int i = 0; i < profileItems.Count; i++)
        {
            profileItems[i].Classes.Remove("Selected");
        }

        selectedProfile = profile;

        if (selectedProfile != null)
        {
            var currentIndex = profilesModel.Elements.IndexOf(selectedProfile);
            if (currentIndex >= 0 && currentIndex < profileItems.Count)
            {
                profileItems[currentIndex].Classes.Add("Selected");
            }
        }
    }

    public BE.IProfileModel? GetSelectedProfile()
    {
        return selectedProfile;
    }


    private void RefreshAllProfileNames()
    {
        for (int i = 0; i < GetProfileCount() && i < profilesModel.Elements.Count; i++)
        {
            if (i >= profileItems.Count) break;

            var border = profileItems[i];
            var profile = profilesModel.Elements[i];

            if (border.Child is Grid grid)
            {
                var textBlock = grid.Children.OfType<TextBlock>().FirstOrDefault();
                if (textBlock != null)
                {
                    textBlock.Text = profile.CurrentNameForDisplay;
                }
            }
        }
    }

    public async Task ExpandElements()
    {
        if (animationHelper == null) return;

        await Task.Delay(animationStateService.Config.ElementRenderDelayMs);
        await animationHelper.ExpandProfileAnimationAsync();

        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(AreAnimationsActive)));
    }

    public async Task CollapseElements()
    {
        if (animationHelper == null) return;

        await animationHelper.CollapseProfileAnimationAsync();

        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(AreAnimationsActive)));
    }

    public bool AreAnimationsActive => animationStateService.AreAnimationsActive;
}