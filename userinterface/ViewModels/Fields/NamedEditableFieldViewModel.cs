using System.ComponentModel;
using userinterface.Services;
using BE = userspace_backend.Model.EditableSettings;

namespace userinterface.ViewModels.Fields
{
    public partial class NamedEditableFieldViewModel : ViewModelBase
    {
        private readonly ILocalizationService localizationService;

        public NamedEditableFieldViewModel(BE.IEditableSetting settingBE, ILocalizationService localizationService)
        {
            SettingBE = settingBE;
            Field = new EditableFieldViewModel(settingBE);
            this.localizationService = localizationService;

            // Subscribe to language changes to update the Name property
            if (localizationService != null)
            {
                localizationService.PropertyChanged += OnLanguageChanged;
            }
        }

        public EditableFieldViewModel Field { get; }

        public string Name => GetLocalizedName();

        protected BE.IEditableSetting SettingBE { get; }

        private string GetLocalizedName()
        {
            // Use the display name directly
            return SettingBE.DisplayName;
        }

        private void OnLanguageChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == LocalizationService.LanguageChangedPropertyName)
            {
                OnPropertyChanged(nameof(Name));
            }
        }
    }
}