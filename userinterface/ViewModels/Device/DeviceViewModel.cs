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

        public DeviceViewModel(BE.IDeviceModel deviceBE, BE.DevicesModel devicesBE, IModalService modalService, bool isDefault = false)
        {
            DeviceBE = deviceBE;
            DevicesBE = devicesBE;
            IsDefaultDevice = isDefault;
            this.modalService = modalService;

            NameField = new LocalizedFieldBase(DeviceBE.Name);
            HWIDField = new LocalizedFieldBase(DeviceBE.HardwareID);
            DPIField = new LocalizedFieldBase(DeviceBE.DPI);
            PollRateField = new LocalizedFieldBase(DeviceBE.PollRate);

            IgnoreField = new LocalizedFieldBase(DeviceBE.Ignore);
            IgnoreField.PropertyChanged += OnIgnoreFieldChanged;

            DeviceGroup = new DeviceGroupSelectorViewModel(DeviceBE, DevicesBE.DeviceGroups);

            DeleteCommand = new RelayCommand(async () => await DeleteWithConfirmation());
        }

        internal BE.IDeviceModel DeviceBE { get; }

        internal BE.DevicesModel DevicesBE { get; }

        public bool IsDefaultDevice { get; }

        public LocalizedFieldBase NameField { get; set; }

        public LocalizedFieldBase HWIDField { get; set; }

        public LocalizedFieldBase DPIField { get; set; }

        public LocalizedFieldBase PollRateField { get; set; }

        public LocalizedFieldBase IgnoreField { get; set; }

        public DeviceGroupSelectorViewModel DeviceGroup { get; set; }

        public ICommand DeleteCommand { get; }

        public bool IsExpanderEnabled => !IsIgnored;

        private bool IsIgnored => bool.TryParse(IgnoreField.ValueText, out var result) && result;

        private bool isDeleting = false;

        private void OnIgnoreFieldChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(LocalizedFieldBase.ValueText))
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
