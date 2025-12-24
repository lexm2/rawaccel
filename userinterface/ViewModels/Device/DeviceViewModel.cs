using System;
using System.Threading.Tasks;
using System.Windows.Input;
using Microsoft.Extensions.DependencyInjection;
using userinterface.Commands;
using userinterface.Services;
using userinterface.ViewModels.Controls;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Device
{
    public partial class DeviceViewModel : ViewModelBase
    {
        private readonly IModalService modalService;

        public DeviceViewModel(BE.IDeviceModel deviceBE, BE.DevicesModel devicesBE, IModalService modalService, LocalizationService localizationService, bool isDefault = false)
        {
            DeviceBE = deviceBE;
            DevicesBE = devicesBE;
            IsDefaultDevice = isDefault;
            this.modalService = modalService;

            NameField = new NamedEditableFieldViewModel(DeviceBE.Name, localizationService);

            HWIDField = new NamedEditableFieldViewModel(DeviceBE.HardwareID, localizationService);

            DPIField = new NamedEditableFieldViewModel(DeviceBE.DPI, localizationService);

            PollRateField = new NamedEditableFieldViewModel(DeviceBE.PollRate, localizationService);

            IgnoreBool = new EditableBoolViewModel(DeviceBE.Ignore, localizationService);
            IgnoreBool.PropertyChanged += OnIgnoreBoolChanged;

            DeviceGroup = new DeviceGroupSelectorViewModel(DeviceBE, DevicesBE.DeviceGroups);

            DeleteCommand = new RelayCommand(async () => await DeleteWithConfirmation());
        }

        internal BE.IDeviceModel DeviceBE { get; }

        internal BE.DevicesModel DevicesBE { get; }

        public bool IsDefaultDevice { get; }

        public NamedEditableFieldViewModel NameField { get; set; }

        public NamedEditableFieldViewModel HWIDField { get; set; }

        public NamedEditableFieldViewModel DPIField { get; set; }

        public NamedEditableFieldViewModel PollRateField { get; set; }

        public EditableBoolViewModel IgnoreBool { get; set; }

        public DeviceGroupSelectorViewModel DeviceGroup { get; set; }

        public ICommand DeleteCommand { get; }

        public bool IsExpanderEnabled => !IgnoreBool.Value;

        private bool isDeleting = false;

        private void OnIgnoreBoolChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EditableBoolViewModel.Value))
            {
                OnPropertyChanged(nameof(IsExpanderEnabled));
            }
        }

        private async Task DeleteWithConfirmation()
        {
            if (isDeleting)
                return;

            isDeleting = true;

            try
            {
                var confirmed = await modalService.ShowConfirmationAsync(
                    "DeviceDeleteTitle",
                    "DeviceDeleteMessage",
                    "DeviceDeleteConfirm",
                    "ModalCancel");

                if (confirmed)
                {
                    DeleteSelf();
                }
            }
            finally
            {
                isDeleting = false;
            }
        }

        public void DeleteSelf()
        {
            bool success = DevicesBE.TryRemoveElement(DeviceBE);
            System.Diagnostics.Debug.Assert(success);
        }
    }
}