using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using userinterface.Services;
using userspace_backend.Logging;
using BE = userspace_backend.Model.AccelDefinitions;
using BEData = userspace_backend.Data.Profiles.Acceleration;

namespace userinterface.ViewModels.Profile
{
    public partial class AccelerationProfileSettingsViewModel : ViewModelBase, IDisposable
    {
        private bool disposed = false;

        public static readonly ObservableCollection<string> DefinitionTypes =
            new(Enum.GetValues(typeof(BEData.AccelerationDefinitionType))
                .Cast<BEData.AccelerationDefinitionType>()
                .Select(d => d.ToString()));

        public static readonly ObservableCollection<string> DefinitionTypeKeys =
            new(Enum.GetValues(typeof(BEData.AccelerationDefinitionType))
                .Cast<BEData.AccelerationDefinitionType>()
                .Select(d => $"AccelDefinition{d}"));

        [ObservableProperty]
        public bool areAccelSettingsVisible;

        public AccelerationProfileSettingsViewModel(BE.IAccelerationModel accelerationBE, INotificationService notificationService, LocalizationService localizationService, IModalService modalService, ILoggingService loggingService, userspace_backend.INotificationManager notificationManager)
        {
            AccelerationBE = accelerationBE;
            var accelModel = (BE.AccelerationModel)accelerationBE;
            AccelerationFormulaSettings = new AccelerationFormulaSettingsViewModel(accelModel.FormulaAccel, notificationService);
            AccelerationLUTSettings = new AccelerationLUTSettingsViewModel(accelModel.LookupTableAccel, loggingService, notificationService, modalService, localizationService, notificationManager);
            AnisotropySettings = new AnisotropyProfileSettingsViewModel(accelerationBE.Anisotropy, localizationService);
            CoalescionSettings = new CoalescionProfileSettingsViewModel(accelerationBE.Coalescion);
            AccelerationBE.Selection.AutoUpdateFromInterface = true;
            AccelerationBE.Selection.PropertyChanged += OnDefinitionTypeChanged;
        }

        public BE.IAccelerationModel AccelerationBE { get; }

        public static ObservableCollection<string> DefinitionTypesLocal => DefinitionTypes;

        public static ObservableCollection<string> DefinitionTypeKeysLocal => DefinitionTypeKeys;

        public AccelerationFormulaSettingsViewModel AccelerationFormulaSettings { get; }

        public AccelerationLUTSettingsViewModel AccelerationLUTSettings { get; }

        public AnisotropyProfileSettingsViewModel AnisotropySettings { get; }

        public CoalescionProfileSettingsViewModel CoalescionSettings { get; }

        private void OnDefinitionTypeChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(AccelerationBE.Selection.ModelValue))
            {
                AreAccelSettingsVisible = true;
            }
        }

        public void Dispose()
        {
            if (disposed)
                return;

            AccelerationBE.Selection.PropertyChanged -= OnDefinitionTypeChanged;

            disposed = true;
            GC.SuppressFinalize(this);
        }
    }
}