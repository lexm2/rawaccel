using Avalonia;
using Avalonia.Controls;
using Avalonia.Styling;
using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Threading.Tasks;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Converters;
using userinterface.Interfaces;
using userinterface.Models;
using userinterface.Services;
using userinterface.ViewModels.Controls;
using userinterface.ViewModels.Device;
using userinterface.ViewModels.Mapping;
using userinterface.ViewModels.Profile;
using userinterface.ViewModels.Settings;
using userinterface.Views;
using IBackEnd = userspace_backend.IBackEnd;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels;

public partial class MainWindowViewModel : ViewModelBase
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CurrentPageContent))]
    private NavigationPage selectedPage = NavigationPage.Devices;

    [ObservableProperty]
    private bool isProfilesExpanded = false;

    // Pre-created ViewModels
    private readonly DevicesPageViewModel devicesPage;
    private readonly ProfilesPageViewModel profilesPage;
    private readonly MappingsPageViewModel mappingsPage;
    private readonly SettingsPageViewModel settingsPage;
    private readonly ProfileListViewModel profileListView;
    private readonly ToastViewModel toastViewModel;

    private readonly IBackEnd backEnd;
    private readonly IThemeService themeService;
    private readonly ISettingsService settingsService;
    private readonly INotificationService notificationService;

    public MainWindowViewModel(
        IBackEnd backEnd,
        IThemeService themeService,
        ISettingsService settingsService,
        INotificationService notificationService,
        DevicesPageViewModel devicesPage,
        ProfilesPageViewModel profilesPage,
        MappingsPageViewModel mappingsPage,
        SettingsPageViewModel settingsPage,
        ProfileListViewModel profileListView,
        ToastViewModel toastViewModel)
    {
        this.backEnd = backEnd ?? throw new ArgumentNullException(nameof(backEnd));
        this.themeService = themeService ?? throw new ArgumentNullException(nameof(themeService));
        this.settingsService = settingsService ?? throw new ArgumentNullException(nameof(settingsService));
        this.notificationService = notificationService ?? throw new ArgumentNullException(nameof(notificationService));
        this.devicesPage = devicesPage ?? throw new ArgumentNullException(nameof(devicesPage));
        this.profilesPage = profilesPage ?? throw new ArgumentNullException(nameof(profilesPage));
        this.mappingsPage = mappingsPage ?? throw new ArgumentNullException(nameof(mappingsPage));
        this.settingsPage = settingsPage ?? throw new ArgumentNullException(nameof(settingsPage));
        this.profileListView = profileListView ?? throw new ArgumentNullException(nameof(profileListView));
        this.toastViewModel = toastViewModel ?? throw new ArgumentNullException(nameof(toastViewModel));

        ApplyCommand = new RelayCommand(() => Apply());
        NavigateCommand = new RelayCommand<NavigationPage>(page => SelectPage(page));
        ToggleThemeCommand = new RelayCommand(() => ToggleTheme());

        profileListView.SelectedProfileChanged += OnProfileSelected;
    }

    public DevicesPageViewModel DevicesPage => devicesPage;

    public ProfilesPageViewModel ProfilesPage => profilesPage;

    public MappingsPageViewModel MappingsPage => mappingsPage;

    public SettingsPageViewModel SettingsPage => settingsPage;

    public ProfileListViewModel ProfileListView => profileListView;

    public ToastViewModel ToastViewModel => toastViewModel;

    protected IBackEnd BackEnd => backEnd;

    public ICommand ApplyCommand { get; }

    public ICommand NavigateCommand { get; }

    public ICommand ToggleThemeCommand { get; }

    public object? CurrentPageContent =>
        SelectedPage switch
        {
            NavigationPage.Devices => DevicesPage,
            NavigationPage.Mappings => MappingsPage,
            NavigationPage.Profiles => ProfilesPage,
            NavigationPage.Settings => SettingsPage,
            _ => DevicesPage
        };

    public void SelectPage(NavigationPage page)
    {
        Console.WriteLine($"SelectPage called with: {page}");
        SelectedPage = page;
        IsProfilesExpanded = page == NavigationPage.Profiles;

        if (page == NavigationPage.Profiles && profileListView.SelectedProfile == null)
        {
            var defaultProfile = backEnd.Profiles.DefaultProfile;
            if (defaultProfile != null)
            {
                profileListView.SelectedProfile = defaultProfile;
            }
            else if (backEnd.Profiles.Profiles.Count > 0)
            {
                profileListView.SelectedProfile = backEnd.Profiles.Profiles[0];
            }
        }

        UpdateNavigationButtonSelection(page);
    }
    
    private void UpdateNavigationButtonSelection(NavigationPage page)
    {
        if (App.Current?.ApplicationLifetime is Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime desktop && 
            desktop.MainWindow is MainWindow mainWindow)
        {
            mainWindow.UpdateNavigationSelection(page);
        }
    }

    public async Task SelectPageAsync(NavigationPage page)
    {
        Console.WriteLine($"SelectPageAsync called with: {page}");
        ViewModelBase pageViewModel = page switch
        {
            NavigationPage.Devices => DevicesPage,
            NavigationPage.Profiles => ProfilesPage,
            NavigationPage.Mappings => MappingsPage,
            NavigationPage.Settings => SettingsPage,
            _ => DevicesPage
        };

        if (pageViewModel is IAsyncInitializable asyncViewModel && !asyncViewModel.IsInitialized)
        {
            Console.WriteLine($"Calling InitializeAsync on {pageViewModel.GetType().Name}");
            await asyncViewModel.InitializeAsync();
        }

        SelectedPage = page;
        IsProfilesExpanded = page == NavigationPage.Profiles;

        if (page == NavigationPage.Profiles && profileListView.SelectedProfile == null)
        {
            var defaultProfile = backEnd.Profiles.DefaultProfile;
            if (defaultProfile != null)
            {
                profileListView.SelectedProfile = defaultProfile;
            }
            else if (backEnd.Profiles.Profiles.Count > 0)
            {
                profileListView.SelectedProfile = backEnd.Profiles.Profiles[0];
            }
        }
    }

    private async void ExpandProfiles()
    {
        var view = App.Current?.ApplicationLifetime is Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime desktop
            ? desktop.MainWindow?.FindControl<userinterface.Views.Profile.ProfileListView>("ProfileListView")
            : null;

        if (view != null)
        {
            await view.ExpandElements();
        }
    }

    private async void CollapseProfiles()
    {
        var view = App.Current?.ApplicationLifetime is Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime desktop
            ? desktop.MainWindow?.FindControl<userinterface.Views.Profile.ProfileListView>("ProfileListView")
            : null;

        if (view != null)
        {
            await view.CollapseElements();
        }
    }

    public void Apply()
    {
        bool success = BackEnd.Apply();
        if (!success)
        {
            notificationService.ShowErrorToast("Notifications.ApplyFailed");
        }
    }

    private void ToggleTheme()
    {
        var currentTheme = settingsService.Theme.ToLower();
        string newTheme;
        
        if (currentTheme == "system")
        {
            var actualSystemTheme = ThemeVariantConverter.GetSystemThemeVariant();
            newTheme = actualSystemTheme == ThemeVariant.Dark ? "Light" : "Dark";
        }
        else
        {
            newTheme = currentTheme == "light" ? "Dark" : "Light";
        }
        
        settingsService.Theme = newTheme;
    }
    
    private void OnProfileSelected(BE.IProfileModel selectedProfile)
    {
        if (selectedProfile != null && SelectedPage != NavigationPage.Profiles)
        {
            SelectPage(NavigationPage.Profiles);
        }
    }

    partial void OnIsProfilesExpandedChanged(bool value)
    {
        if (value)
        {
            ExpandProfiles();
        }
        else
        {
            CollapseProfiles();
        }
    }
}