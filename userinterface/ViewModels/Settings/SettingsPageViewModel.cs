using System;
using userinterface.Services;

namespace userinterface.ViewModels.Settings;

public class SettingsPageViewModel : ViewModelBase
{
    private readonly INotificationService? notificationService;

    public SettingsPageViewModel(
        INotificationService? notificationService,
        GeneralSettingsViewModel generalSettingsViewModel,
        SupportViewModel supportViewModel)
    {
        this.notificationService = notificationService;
        GeneralSettingsViewModel = generalSettingsViewModel ?? throw new ArgumentNullException(nameof(generalSettingsViewModel));
        SupportViewModel = supportViewModel ?? throw new ArgumentNullException(nameof(supportViewModel));

        GeneralSettingsViewModel.PropertyChanged += OnGeneralSettingsChanged;
    }

    public GeneralSettingsViewModel GeneralSettingsViewModel { get; }

    public SupportViewModel SupportViewModel { get; }

    private void OnGeneralSettingsChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        // Language change notification is now handled in ChangeLanguage method (leave this here for future usage)
    }
}