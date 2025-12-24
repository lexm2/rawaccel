using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Linq;
using System.Threading.Tasks;
using userinterface.Services;
using userinterface.ViewModels.Profile;
using userspace_backend;
using BE = userspace_backend.Model;

namespace userinterface.Views.Profile;

public partial class ProfileListView : UserControl
{
    private readonly List<Border> allItems = [];
    private Panel? profileContainer;
    private readonly BE.IProfilesModel profilesModel;
    private BE.IProfileModel? selectedProfile;

    private int GetProfileCount() => allItems.Count - 1;

    private readonly IModalService modalService;
    private readonly LocalizationService localizationService;
    private TextBlock? addProfileTextBlock;

    // Layout constants
    private const double ProfileHeight = 40;
    private const double ProfileSpacing = 8;
    private const double FirstIndexOffset = 8;

    public ProfileListView()
    {
        var backEnd = App.Services?.GetRequiredService<IBackEnd>() ?? throw new InvalidOperationException("BackEnd service not available");
        modalService = App.Services?.GetRequiredService<IModalService>() ?? throw new InvalidOperationException("ModalService not available");
        localizationService = App.Services?.GetRequiredService<LocalizationService>() ?? throw new InvalidOperationException("LocalizationService not available");

        profilesModel = backEnd.Profiles ?? throw new ArgumentNullException(nameof(backEnd.Profiles));
        localizationService.PropertyChanged += OnLocalizationPropertyChanged;
        ((INotifyCollectionChanged)profilesModel.Profiles).CollectionChanged += OnProfilesCollectionChanged;

        InitializeComponent();

        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnUnloaded(object? sender, RoutedEventArgs e)
    {
        if (localizationService != null)
        {
            localizationService.PropertyChanged -= OnLocalizationPropertyChanged;
        }
    }

    private void OnLoaded(object? sender, RoutedEventArgs e)
    {
        profileContainer = this.FindControl<Panel>("ProfileContainer");

        if (DataContext is ProfileListViewModel viewModel)
        {
            viewModel.SetView(this);
        }

        var addButton = CreateAddProfileButton();
        allItems.Add(addButton);
        profileContainer?.Children.Add(addButton);

        CreateProfiles();
        SetAllElementPositions();
    }

    private void OnLocalizationPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (addProfileTextBlock != null)
        {
            addProfileTextBlock.Text = localizationService?.GetText("ProfileAddNewProfile") ?? "Add New Profile";
        }
    }

    private void OnProfilesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        switch (e.Action)
        {
            case NotifyCollectionChangedAction.Add:
                HandleProfilesAdded(e);
                break;
            case NotifyCollectionChangedAction.Remove:
                HandleProfilesRemoved(e);
                break;
            case NotifyCollectionChangedAction.Replace:
                HandleProfilesReplaced(e);
                break;
            case NotifyCollectionChangedAction.Move:
                HandleProfilesMoved(e);
                break;
            case NotifyCollectionChangedAction.Reset:
                HandleProfilesReset();
                break;
        }
    }

    private void HandleProfilesAdded(NotifyCollectionChangedEventArgs e)
    {
        if (e.NewItems == null) return;

        int startIndex = e.NewStartingIndex >= 0 ? e.NewStartingIndex : profilesModel.Profiles.Count - e.NewItems.Count;

        for (int i = 0; i < e.NewItems.Count; i++)
        {
            int profileIndex = startIndex + i;
            AddProfileAtPosition(profileIndex);
        }

        RefreshAllProfileNames();

        if (e.NewItems.Count > 0)
        {
            int lastAddedIndex = startIndex + e.NewItems.Count - 1;
            if (lastAddedIndex >= 0 && lastAddedIndex < profilesModel.Profiles.Count)
            {
                SetSelectedProfile(profilesModel.Profiles[lastAddedIndex]);
            }
        }
    }

    private void HandleProfilesRemoved(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems == null) return;

        int removeIndex = e.OldStartingIndex >= 0 ? e.OldStartingIndex : GetProfileCount() - 1;
        int removeCount = e.OldItems.Count;

        for (int i = 0; i < removeCount && removeIndex >= 0 && removeIndex < GetProfileCount(); i++)
        {
            RemoveProfileAt(removeIndex);
        }

        UpdateAllZIndexes();
        SetAllElementPositions();

        if (selectedProfile != null && !profilesModel.Profiles.Contains(selectedProfile))
        {
            var defaultProfile = profilesModel.DefaultProfile;
            if (defaultProfile != null)
            {
                SetSelectedProfile(defaultProfile);
            }
            else if (profilesModel.Profiles.Count > 0)
            {
                SetSelectedProfile(profilesModel.Profiles[0]);
            }
            else
            {
                SetSelectedProfile(null);
            }
        }
    }

    private void HandleProfilesReplaced(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems == null || e.NewItems == null || e.OldStartingIndex < 0) return;

        int replaceIndex = e.OldStartingIndex;
        int itemCount = Math.Min(e.OldItems.Count, e.NewItems.Count);

        for (int i = 0; i < itemCount && replaceIndex + i < GetProfileCount(); i++)
        {
            int itemIndex = replaceIndex + i + 1;
            if (itemIndex < allItems.Count && allItems[itemIndex].Child is Grid grid)
            {
                var textBlock = grid.Children.OfType<TextBlock>().FirstOrDefault();
                if (textBlock != null)
                {
                    textBlock.Text = $"Profile {replaceIndex + i + 1}";
                }
            }
        }

        UpdateAllZIndexes();
        SetAllElementPositions();
    }

    private void HandleProfilesMoved(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldStartingIndex < 0 || e.NewStartingIndex < 0) return;

        MoveProfile(e.OldStartingIndex, e.NewStartingIndex);
        UpdateAllZIndexes();
        SetAllElementPositions();
    }

    private void HandleProfilesReset()
    {
        var addButton = allItems.Count > 0 ? allItems[0] : null;
        allItems.Clear();
        profileContainer?.Children.Clear();

        if (addButton != null)
        {
            allItems.Add(addButton);
            profileContainer?.Children.Add(addButton);
        }

        for (int i = 0; i < profilesModel.Profiles.Count; i++)
        {
            AddProfileAtPosition(i);
        }

        UpdateAllZIndexes();
        SetAllElementPositions();
    }

    private void RemoveProfileAt(int index)
    {
        int itemIndex = index + 1;
        if (itemIndex < 0 || itemIndex >= allItems.Count) return;

        var item = allItems[itemIndex];
        allItems.RemoveAt(itemIndex);
        profileContainer?.Children.Remove(item);
    }

    private void MoveProfile(int fromIndex, int toIndex)
    {
        int fromItemIndex = fromIndex + 1;
        int toItemIndex = toIndex + 1;

        if (fromItemIndex < 1 || fromItemIndex >= allItems.Count ||
            toItemIndex < 1 || toItemIndex >= allItems.Count ||
            fromItemIndex == toItemIndex) return;

        var item = allItems[fromItemIndex];
        allItems.RemoveAt(fromItemIndex);
        allItems.Insert(toItemIndex, item);

        profileContainer?.Children.RemoveAt(fromItemIndex);
        profileContainer?.Children.Insert(toItemIndex, item);
    }

    private void AddProfileAtPosition(int targetIndex)
    {
        if (targetIndex < 0 || targetIndex > GetProfileCount()) return;

        var profileBorder = CreateProfileBorder(null!, targetIndex);
        profileBorder.ZIndex = 1000;
        profileBorder.Opacity = 1.0;

        int itemIndex = targetIndex + 1;
        allItems.Insert(itemIndex, profileBorder);
        profileContainer?.Children.Insert(itemIndex, profileBorder);

        UpdateAllZIndexes();
        SetAllElementPositions();
    }

    private Border CreateAddProfileButton()
    {
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
            Height = ProfileHeight,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(8, 0, 8, ProfileSpacing),
            Child = addProfileTextBlock
        };

        border.PointerPressed += (s, e) => OnAddProfileClicked(s!, e);

        return border;
    }

    private Border CreateProfileBorder(IBrush color, int targetIndex)
    {
        var profileName = targetIndex < profilesModel.Profiles.Count ? profilesModel.Profiles[targetIndex].CurrentNameForDisplay : $"Profile {targetIndex + 1}";
        var isDefaultProfile = targetIndex < profilesModel.Profiles.Count && profilesModel.Profiles[targetIndex] == profilesModel.DefaultProfile;

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
            Height = ProfileHeight,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(8, 0, 8, ProfileSpacing),
            Child = grid,
            Opacity = 1.0,
            ZIndex = targetIndex
        };

        border.PointerPressed += OnProfileBorderClicked;

        return border;
    }

    private void OnProfileBorderClicked(object? sender, Avalonia.Input.PointerPressedEventArgs e)
    {
        if (sender is Border border)
        {
            int profileIndex = allItems.IndexOf(border) - 1;
            if (profileIndex >= 0 && profileIndex < profilesModel.Profiles.Count)
            {
                var clickedProfile = profilesModel.Profiles[profileIndex];
                SetSelectedProfile(clickedProfile);
            }
        }
    }

    private void OnAddProfileClicked(object sender, RoutedEventArgs e)
    {
        if (DataContext is ProfileListViewModel viewModel)
        {
            viewModel.TryAddProfile();
        }
    }

    private async void OnDeleteButtonClicked(object? sender, RoutedEventArgs e)
    {
        if (sender is Button deleteButton &&
            deleteButton.Parent is Grid grid &&
            grid.Parent is Border border)
        {
            var profileIndex = allItems.IndexOf(border) - 1;
            if (profileIndex >= 0 && profileIndex < profilesModel.Profiles.Count)
            {
                var profileToDelete = profilesModel.Profiles[profileIndex];

                var confirmed = await modalService.ShowConfirmationAsync(
                    "ProfileDeleteTitle",
                    "ProfileDeleteMessage",
                    "ProfileDeleteConfirm",
                    "ModalCancel");

                if (confirmed)
                {
                    profilesModel.RemoveProfile(profileToDelete);
                }
            }
        }
    }

    private double CalculatePositionForIndex(int itemIndex)
    {
        return itemIndex == 0 ? 0 : (itemIndex * (ProfileHeight + ProfileSpacing)) + FirstIndexOffset;
    }

    private void UpdateAllZIndexes()
    {
        for (int i = 0; i < allItems.Count; i++)
        {
            allItems[i].ZIndex = i;
        }
    }

    private void CreateProfiles()
    {
        for (int i = 0; i < profilesModel.Profiles.Count; i++)
        {
            var profileBorder = CreateProfileBorder(null!, i);
            profileBorder.ZIndex = 1000;
            profileBorder.Opacity = 1.0;

            int itemIndex = i + 1;
            allItems.Insert(itemIndex, profileBorder);
            profileContainer?.Children.Insert(itemIndex, profileBorder);
        }

        UpdateAllZIndexes();
        RefreshAllProfileNames();
    }

    private void SetAllElementPositions()
    {
        for (int i = 0; i < allItems.Count; i++)
        {
            var element = allItems[i];
            var targetY = CalculatePositionForIndex(i);
            element.Margin = new Thickness(8, targetY, 8, ProfileSpacing);
            element.ZIndex = i;
        }
    }

    public void SetSelectedProfile(BE.IProfileModel? profile, bool updateViewModel = true)
    {
        if (selectedProfile == profile) return;

        for (int i = 1; i < allItems.Count; i++)
        {
            allItems[i].Classes.Remove("Selected");
        }

        selectedProfile = profile;

        if (updateViewModel && DataContext is ProfileListViewModel viewModel)
        {
            viewModel.SelectedProfile = selectedProfile;
        }

        if (selectedProfile != null)
        {
            var currentIndex = profilesModel.Profiles.IndexOf(selectedProfile);
            if (currentIndex >= 0 && currentIndex < GetProfileCount())
            {
                int itemIndex = currentIndex + 1;
                if (itemIndex < allItems.Count)
                {
                    allItems[itemIndex].Classes.Add("Selected");
                }
            }
        }
    }

    public BE.IProfileModel? GetSelectedProfile()
    {
        return selectedProfile;
    }

    private void RefreshAllProfileNames()
    {
        for (int i = 0; i < GetProfileCount() && i < profilesModel.Profiles.Count; i++)
        {
            int itemIndex = i + 1;
            if (itemIndex >= allItems.Count) break;

            var border = allItems[itemIndex];
            var profile = profilesModel.Profiles[i];

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

    public Task ExpandElements()
    {
        SetAllElementPositions();
        return Task.CompletedTask;
    }

    public Task CollapseElements()
    {
        // Set all elements to position 0 (collapsed)
        for (int i = 0; i < allItems.Count; i++)
        {
            allItems[i].Margin = new Thickness(8, 0, 8, ProfileSpacing);
        }
        return Task.CompletedTask;
    }
}
