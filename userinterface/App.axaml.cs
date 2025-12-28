using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Data.Core.Plugins;
using Avalonia.Markup.Xaml;
using Avalonia.Styling;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Diagnostics;
using System.Security.AccessControl;
using System.Threading.Tasks;
using userinterface.Services;
using userinterface.Services.Events;
using userinterface.ViewModels;
using userinterface.ViewModels.Controls;
using userinterface.ViewModels.Settings;
using userinterface.Views;
using userspace_backend;
using userspace_backend.IO;
#if WINDOWS
using userspace_backend.Driver.Windows;
#else
using userspace_backend.Driver.Debug;
#endif

namespace userinterface;

public partial class App : Application
{
    public static IServiceProvider? Services { get; private set; }

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        var services = new ServiceCollection();

        services.AddLogging(builder =>
        {
#if DEBUG
            builder.AddDebug();
            builder.SetMinimumLevel(LogLevel.Warning);
#else
            builder.SetMinimumLevel(LogLevel.Warning);
#endif
        });

#if WINDOWS
        // Windows: Use BackEndLoader to read from JSON files
        string settingsDirectory = System.AppDomain.CurrentDomain.BaseDirectory;
        services.AddSingleton<IBackEndLoader>(sp =>
        {
            var devicesRW = sp.GetRequiredService<DevicesReaderWriter>();
            var mappingsRW = sp.GetRequiredService<MappingsReaderWriter>();
            var profileRW = sp.GetRequiredService<ProfileReaderWriter>();
            var settingsRW = sp.GetRequiredService<SettingsReaderWriter>();
            return new BackEndLoader(settingsDirectory, devicesRW, mappingsRW, profileRW, settingsRW);
        });
#endif
        // Non-Windows: IBackEndLoader is registered by AddDebugDriver()

        services.AddSingleton<INotificationService>(provider =>
            new NotificationService(
                provider.GetRequiredService<IEventBus>(),
                provider.GetRequiredService<LocalizationService>(),
                provider.GetRequiredService<ISettingsService>()));
        services.AddSingleton<IModalService>(provider =>
            new ModalService(provider.GetRequiredService<LocalizationService>(), provider.GetRequiredService<ISettingsService>()));
        services.AddSingleton<IThemeService>(provider =>
            new ThemeService(provider.GetRequiredService<ISettingsService>()));
        services.AddSingleton<IViewModelFactory, ViewModelFactory>();
        services.AddSingleton<LocalizationService>();
        services.AddSingleton<PreviewChartRenderer>();
        services.AddSingleton<ISettingsService, SettingsService>();
        services.AddSingleton<IEventBus, EventBus>();

        RegisterViewModels(services);

#if WINDOWS
        Services = BackEndComposer.Compose(services, s => s.AddWindowsDriver());
#else
        Services = BackEndComposer.Compose(services, s => s.AddDebugDriver());
#endif

        IBackEnd backEnd = Services.GetRequiredService<IBackEnd>();
        backEnd.Load();

        ApplyStartupSettings();

        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            // Line below is needed to remove Avalonia data validation.
            // Without this line you will get duplicate validations from both Avalonia and CT
            BindingPlugins.DataValidators.RemoveAt(0);

            var mainWindow = new MainWindow()
            {
                DataContext = Services.GetRequiredService<MainWindowViewModel>(),
            };

            var toastView = mainWindow.FindControl<Views.Controls.ToastView>("ToastView");
            if (toastView != null)
            {
                toastView.DataContext = Services.GetRequiredService<ToastViewModel>();
            }

            desktop.MainWindow = mainWindow;

            // Preload libraries that cause first-page stutter
            _ = PreloadLibrariesAsync();

            // Show alpha build warning modal
            _ = ShowAlphaBuildWarningAsync();

#if DEBUG
            desktop.MainWindow.AttachDevTools();
#endif
        }

        base.OnFrameworkInitializationCompleted();
    }

    private void RegisterViewModels(IServiceCollection services)
    {
        // Control ViewModels (registered first as they may be dependencies)
        services.AddSingleton<ViewModels.Controls.ToastViewModel>(provider =>
            new ViewModels.Controls.ToastViewModel(
                provider.GetRequiredService<IEventBus>(),
                provider.GetRequiredService<LocalizationService>()));

        // Device ViewModels
        services.AddSingleton<ViewModels.Device.DevicesPageViewModel>(provider =>
            new ViewModels.Device.DevicesPageViewModel(
                provider.GetRequiredService<IBackEnd>(),
                provider.GetRequiredService<IModalService>(),
                provider.GetRequiredService<LocalizationService>()));
        services.AddTransient<ViewModels.Device.DevicesListViewModel>(provider =>
            new ViewModels.Device.DevicesListViewModel(
                provider.GetRequiredService<IBackEnd>().Devices,
                provider.GetRequiredService<IModalService>(),
                provider.GetRequiredService<LocalizationService>()));
        services.AddTransient<ViewModels.Device.DeviceGroupsViewModel>();
        services.AddTransient<ViewModels.Device.DeviceGroupViewModel>();
        services.AddTransient<ViewModels.Device.DeviceGroupSelectorViewModel>();
        services.AddTransient<ViewModels.Device.DeviceViewModel>();

        // Profile ViewModels
        services.AddSingleton<ViewModels.Profile.ProfilesPageViewModel>();
        services.AddSingleton<ViewModels.Profile.ProfileListViewModel>();
        services.AddTransient<ViewModels.Profile.ProfileViewModel>();
        services.AddTransient<ViewModels.Profile.ProfileSettingsViewModel>(provider =>
            new ViewModels.Profile.ProfileSettingsViewModel(
                provider.GetRequiredService<INotificationService>(),
                provider.GetRequiredService<LocalizationService>()));
        services.AddTransient<ViewModels.Profile.ProfileChartViewModel>();
        services.AddTransient<ViewModels.Profile.AccelerationFormulaSettingsViewModel>();
        services.AddTransient<ViewModels.Profile.AccelerationLUTSettingsViewModel>();
        services.AddTransient<ViewModels.Profile.AccelerationProfileSettingsViewModel>();
        services.AddTransient<ViewModels.Profile.AnisotropyProfileSettingsViewModel>();
        services.AddTransient<ViewModels.Profile.CoalescionProfileSettingsViewModel>();
        services.AddTransient<ViewModels.Profile.HiddenProfileSettingsViewModel>();

        // Mapping ViewModels
        services.AddSingleton<ViewModels.Mapping.MappingsPageViewModel>();
        services.AddTransient<ViewModels.Mapping.MappingViewModel>();
        services.AddTransient<ViewModels.Mapping.MappingListElementViewModel>();

        // Settings ViewModels
        services.AddSingleton<ViewModels.Settings.SupportViewModel>();
        services.AddSingleton<ViewModels.Settings.GeneralSettingsViewModel>(provider =>
            new ViewModels.Settings.GeneralSettingsViewModel(
                provider.GetRequiredService<ISettingsService>(),
                provider.GetRequiredService<LocalizationService>(),
                provider.GetRequiredService<IThemeService>()));
        services.AddSingleton<SettingsPageViewModel>(provider =>
            new SettingsPageViewModel(
                provider.GetService<INotificationService>(),
                provider.GetRequiredService<ViewModels.Settings.GeneralSettingsViewModel>(),
                provider.GetRequiredService<ViewModels.Settings.SupportViewModel>()));

        // Main ViewModels (registered last as they depend on other ViewModels)
        services.AddSingleton<MainWindowViewModel>(provider =>
            new MainWindowViewModel(
                provider.GetRequiredService<IBackEnd>(),
                provider.GetRequiredService<IThemeService>(),
                provider.GetRequiredService<ISettingsService>(),
                provider.GetRequiredService<INotificationService>(),
                provider.GetRequiredService<ViewModels.Device.DevicesPageViewModel>(),
                provider.GetRequiredService<ViewModels.Profile.ProfilesPageViewModel>(),
                provider.GetRequiredService<ViewModels.Mapping.MappingsPageViewModel>(),
                provider.GetRequiredService<SettingsPageViewModel>(),
                provider.GetRequiredService<ViewModels.Profile.ProfileListViewModel>(),
                provider.GetRequiredService<ViewModels.Controls.ToastViewModel>()));
    }

    private async Task ShowAlphaBuildWarningAsync()
    {
        var modalService = Services?.GetService<IModalService>();
        if (modalService != null)
        {
            var warningView = new Views.Controls.AlphaBuildWarningView();
            await modalService.ShowDialogAsync<bool>(warningView);
        }
    }

    public static void OpenBugReportUrl()
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "https://github.com/RawAccelOfficial/rawaccel/issues",
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to open bug report URL: {ex.Message}");
        }
    }

    public static void OpenDiscordUrl()
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "https://discord.gg/7pQh8zH",
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to open Discord URL: {ex.Message}");
        }
    }

    /* 
     * This was originally intended to preload libraries that cause stutter 
     * but it seems to not have much effect. Will leave it here for now.
     * 
     * Could also do these
     * System.Runtime.Intrinsics
     * System.Text.Json
     * System.Text.Encodings.Web
     * System.Text.Encoding.Extensions
     * System.IO.Pipelines
    */
    private async Task PreloadLibrariesAsync()
    {
        try
        {
            Debug.WriteLine("[PRELOAD] Starting library preload...");

            await Task.Run(() =>
            {
                try
                {
                    _ = typeof(LiveChartsCore.SkiaSharpView.Avalonia.CartesianChart).Assembly;

                    _ = typeof(SkiaSharp.HarfBuzz.SKShaper).Assembly;

                    _ = typeof(SkiaSharp.SKCanvas).Assembly;

                    _ = typeof(LiveChartsCore.CartesianChart<>).Assembly;

                    _ = typeof(Avalonia.Controls.ItemsRepeater).Assembly;
                    
                    _ = typeof(System.Security.Cryptography.MD5).Assembly;
                    
                    _ = typeof(Avalonia.Media.Imaging.Bitmap).Assembly;
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[PRELOAD] Library loading failed: {ex.Message}");
                }
            });

            Debug.WriteLine("[PRELOAD] All libraries preloaded successfully");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[PRELOAD] Preload task failed: {ex.Message}");
        }
    }

    private void ApplyStartupSettings()
    {
        try
        {
            var settingsService = Services?.GetService<ISettingsService>();
            var localizationService = Services?.GetService<LocalizationService>();
            var themeService = Services?.GetService<IThemeService>();

            if (settingsService != null && localizationService != null)
            {
                // Apply language setting from backend
                localizationService.ChangeLanguage(settingsService.Language);
            }

            if (themeService != null)
            {
                // Apply theme setting from backend
                themeService.ApplyThemeFromSettings();
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[STARTUP] Failed to apply startup settings: {ex.Message}");
        }
    }
}