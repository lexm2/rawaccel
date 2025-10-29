using Avalonia.Controls;
using Avalonia.Styling;
using System;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Converters;
using userinterface.Interfaces;
using userinterface.Models;
using userinterface.Services;
using userinterface.ViewModels.Fields;
using userinterface.ViewModels.Device;
using userinterface.ViewModels.Mapping;
using userinterface.ViewModels.Profile;
using userinterface.ViewModels.Settings;
using userinterface.Views;
// using userspace_backend.Logging;
using BE = userspace_backend;

namespace userinterface.ViewModels;

public partial class MainWindowViewModel : ViewModelBase, INotifyPropertyChanged, IDisposable
{
    private bool disposed = false;
    private NavigationPage selectedPageValue = NavigationPage.Devices;
    private bool isProfilesExpandedValue = false;

    // Pre-created ViewModels
    private readonly DevicesPageViewModel devicesPage;
    private readonly ProfilesPageViewModel profilesPage;
    private readonly MappingsPageViewModel mappingsPage;
    private readonly SettingsPageViewModel settingsPage;
    private readonly ProfileListViewModel profileListView;
    private readonly ToastContainerViewModel toastContainerViewModel;
    private readonly IModalService modalService;
    private readonly ISettingsService settingsService;

    private readonly BE.IBackEnd backEnd;
    private readonly IThemeService themeService;
    private readonly INotificationService notificationService;
    private readonly IFrameTimerService frameTimer;
    private readonly BE.INotificationManager notificationManager;

    public MainWindowViewModel(
        BE.IBackEnd backEnd,
        IThemeService themeService,
        ISettingsService settingsService,
        IFrameTimerService frameTimer,
        INotificationService notificationService,
        BE.INotificationManager notificationManager,
        DevicesPageViewModel devicesPage,
        ProfilesPageViewModel profilesPage,
        MappingsPageViewModel mappingsPage,
        SettingsPageViewModel settingsPage,
        ProfileListViewModel profileListView,
        ToastContainerViewModel toastContainerViewModel,
        IModalService modalService)
    {
        this.backEnd = backEnd ?? throw new ArgumentNullException(nameof(backEnd));
        this.themeService = themeService ?? throw new ArgumentNullException(nameof(themeService));
        this.settingsService = settingsService ?? throw new ArgumentNullException(nameof(settingsService));
        this.notificationService = notificationService ?? throw new ArgumentNullException(nameof(notificationService));
        this.frameTimer = frameTimer ?? throw new ArgumentNullException(nameof(frameTimer));
        this.notificationManager = notificationManager ?? throw new ArgumentNullException(nameof(notificationManager));
        this.devicesPage = devicesPage ?? throw new ArgumentNullException(nameof(devicesPage));
        this.profilesPage = profilesPage ?? throw new ArgumentNullException(nameof(profilesPage));
        this.mappingsPage = mappingsPage ?? throw new ArgumentNullException(nameof(mappingsPage));
        this.settingsPage = settingsPage ?? throw new ArgumentNullException(nameof(settingsPage));
        this.profileListView = profileListView ?? throw new ArgumentNullException(nameof(profileListView));
        this.toastContainerViewModel = toastContainerViewModel ?? throw new ArgumentNullException(nameof(toastContainerViewModel));
        this.modalService = modalService ?? throw new ArgumentNullException(nameof(modalService));

        backEnd.LoggingService?.LogInformation(userspace_backend.Logging.LogSource.UI, "MainWindowViewModel initializing");

        ApplyCommand = new RelayCommand(() => Apply());
        NavigateCommand = new RelayCommand<NavigationPage>(page => SelectPage(page));
        ToggleThemeCommand = new RelayCommand(() => ToggleTheme());

        profileListView.SelectedProfileChanged += OnProfileSelected;
        notificationManager.NotificationRequested += OnBackEndNotificationRequested;
        notificationManager.QueuedNotificationRequested += OnBackEndQueuedNotificationRequested;

        backEnd.LoggingService?.LogInformation(userspace_backend.Logging.LogSource.UI, "MainWindowViewModel initialized");
    }

    public DevicesPageViewModel DevicesPage => devicesPage;

    public ProfilesPageViewModel ProfilesPage => profilesPage;

    public MappingsPageViewModel MappingsPage => mappingsPage;

    public SettingsPageViewModel SettingsPage => settingsPage;

    public ProfileListViewModel ProfileListView => profileListView;

    public ToastContainerViewModel ToastContainerViewModel => toastContainerViewModel;

    protected BE.IBackEnd BackEnd => backEnd;

    public ICommand ApplyCommand { get; }

    public ICommand NavigateCommand { get; }

    public ICommand ToggleThemeCommand { get; }

    public NavigationPage SelectedPage
    {
        get => selectedPageValue;
        set
        {
            if (selectedPageValue != value)
            {
                selectedPageValue = value;
                OnPropertyChanged();
                OnPropertyChanged(nameof(CurrentPageContent));
            }
        }
    }

    public bool IsProfilesExpanded
    {
        get => isProfilesExpandedValue;
        set
        {
            if (isProfilesExpandedValue != value)
            {
                // Check if force profiles list open is enabled before allowing collapse
                if (!value && settingsService.ForceProfilesListOpen)
                {
                    return; // Don't collapse if force setting is enabled
                }

                isProfilesExpandedValue = value;
                OnPropertyChanged();

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
    }

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
        backEnd.LoggingService?.LogDebug(userspace_backend.Logging.LogSource.UI, "Navigating to page: {PageName}", page);
        SelectedPage = page;
        IsProfilesExpanded = page == NavigationPage.Profiles;

        if (page == NavigationPage.Profiles && profileListView.SelectedProfile == null)
        {
            // Select first profile as default
            if (backEnd.Profiles.Elements.Count > 0)
            {
                profileListView.SelectedProfile = backEnd.Profiles.Elements[0];
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
            // Select first profile as default
            if (backEnd.Profiles.Elements.Count > 0)
            {
                profileListView.SelectedProfile = backEnd.Profiles.Elements[0];
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

    public bool Apply()
    {
        backEnd.LoggingService?.LogInformation(userspace_backend.Logging.LogSource.UI, "Apply settings requested from UI");
        BackEnd.Apply();
        backEnd.LoggingService?.LogInformation(userspace_backend.Logging.LogSource.UI, "Apply settings completed");
        return true;
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

    private void OnProfileSelected(BE.Model.IProfileModel selectedProfile)
    {
        if (selectedProfile != null && SelectedPage != NavigationPage.Profiles)
        {
            SelectPage(NavigationPage.Profiles);
        }
    }

    private void OnBackEndNotificationRequested(object? sender, BE.NotificationEventArgs e)
    {
        var toastType = e.Type switch
        {
            BE.NotificationType.Info => ToastType.Info,
            BE.NotificationType.Success => ToastType.Success,
            BE.NotificationType.Warning => ToastType.Warning,
            BE.NotificationType.Error => ToastType.Error,
            _ => ToastType.Info
        };

        if (e.FormatArgs.Length > 0)
        {
            notificationService.ShowToast(e.MessageKey, toastType, 5000, e.FormatArgs);
        }
        else
        {
            notificationService.ShowToast(e.MessageKey, toastType);
        }
    }

    private void OnBackEndQueuedNotificationRequested(object? sender, BE.NotificationEventArgs e)
    {
        var toastType = e.Type switch
        {
            BE.NotificationType.Info => ToastType.Info,
            BE.NotificationType.Success => ToastType.Success,
            BE.NotificationType.Warning => ToastType.Warning,
            BE.NotificationType.Error => ToastType.Error,
            _ => ToastType.Info
        };

        if (e.FormatArgs.Length > 0)
        {
            notificationService.ShowToast(e.MessageKey, toastType, 5000, e.FormatArgs);
        }
        else
        {
            notificationService.ShowToast(e.MessageKey, toastType);
        }
    }

    public new event PropertyChangedEventHandler? PropertyChanged;

    protected virtual new void OnPropertyChanged([CallerMemberName] string? PropertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(PropertyName));
    }

    public void Dispose()
    {
        if (disposed)
            return;

        profileListView.SelectedProfileChanged -= OnProfileSelected;
        notificationManager.NotificationRequested -= OnBackEndNotificationRequested;
        notificationManager.QueuedNotificationRequested -= OnBackEndQueuedNotificationRequested;

        disposed = true;
        GC.SuppressFinalize(this);
    }
}