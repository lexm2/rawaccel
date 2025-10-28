using System;
using userinterface.Services;

namespace userinterface.ViewModels.Settings;

public class ProfilesSettingsViewModel : ViewModelBase
{
    private readonly ISettingsService settingsService;

    public ProfilesSettingsViewModel(ISettingsService settingsService)
    {
        this.settingsService = settingsService ?? throw new ArgumentNullException(nameof(settingsService));
    }

    public bool ForceProfilesListOpen
    {
        get => settingsService.ForceProfilesListOpen;
        set
        {
            settingsService.ForceProfilesListOpen = value;
            OnPropertyChanged();
        }
    }
}