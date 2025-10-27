using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Services;
using userinterface.ViewModels.Controls;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Device
{
    public partial class DeviceViewModel : ViewModelBase
    {
        private readonly IModalService modalService;
        private readonly userspace_backend.BackEnd? backEnd;

        public DeviceViewModel(BE.IDeviceModel deviceBE, BE.IDevicesModel devicesBE, IModalService modalService, LocalizationService localizationService, bool isDefault = false, Func<DeviceViewModel, Task>? animatedDeleteCallback = null)
        {
            DeviceBE = deviceBE;
            DevicesBE = devicesBE;
            IsDefaultDevice = isDefault;
            AnimatedDeleteCallback = animatedDeleteCallback;
            this.modalService = modalService;
            backEnd = App.Services?.GetService<userspace_backend.BackEnd>();

            NameField = new NamedEditableFieldViewModel(DeviceBE.Name, localizationService);

            HWIDField = new NamedEditableFieldViewModel(DeviceBE.HardwareID, localizationService);

            DPIField = new NamedEditableFieldViewModel(DeviceBE.DPI, localizationService);

            PollRateField = new NamedEditableFieldViewModel(DeviceBE.PollRate, localizationService);

            IgnoreBool = new EditableBoolViewModel(DeviceBE.Ignore, localizationService);
            IgnoreBool.PropertyChanged += OnIgnoreBoolChanged;

            DeviceGroup = new DeviceGroupSelectorViewModel(DeviceBE, ((BE.DevicesModel)DevicesBE).DeviceGroups);

            AvailableDevices = new ObservableCollection<BE.ISystemDevice>();
            RefreshAvailableDevices();

            var currentDevice = AvailableDevices.FirstOrDefault(d => d.HWID == DeviceBE.HardwareID.ModelValue);
            SelectedDevice = currentDevice;

            DeleteCommand = new RelayCommand(async () => await DeleteWithAnimation());
            RefreshDevicesCommand = new RelayCommand(RefreshAvailableDevices);
        }

        internal BE.IDeviceModel DeviceBE { get; }

        internal BE.IDevicesModel DevicesBE { get; }

        public bool IsDefaultDevice { get; }

        private Func<DeviceViewModel, Task>? AnimatedDeleteCallback { get; }

        public NamedEditableFieldViewModel NameField { get; set; }

        public NamedEditableFieldViewModel HWIDField { get; set; }

        public NamedEditableFieldViewModel DPIField { get; set; }

        public NamedEditableFieldViewModel PollRateField { get; set; }

        public EditableBoolViewModel IgnoreBool { get; set; }

        public DeviceGroupSelectorViewModel DeviceGroup { get; set; }

        public ObservableCollection<BE.ISystemDevice> AvailableDevices { get; set; }

        private BE.ISystemDevice? selectedDevice;

        public BE.ISystemDevice? SelectedDevice
        {
            get => selectedDevice;
            set
            {
                if (SetProperty(ref selectedDevice, value))
                {
                    if (value != null)
                    {
                        DeviceBE.HardwareID.InterfaceValue = value.HWID;
                        DeviceBE.HardwareID.TryUpdateFromInterface();
                    }
                }
            }
        }

        public ICommand DeleteCommand { get; }

        public ICommand RefreshDevicesCommand { get; }

        public bool IsExpanderEnabled => !IgnoreBool.Value;

        public bool IsActiveDevice
        {
            get
            {
                // Hardware detection has been removed - no device is considered "active"
                return false;
            }
        }

        private bool isDeleting = false;

        private void RefreshAvailableDevices()
        {
            var devicesModel = (BE.DevicesModel)DevicesBE;
            devicesModel.SystemDevices.RefreshSystemDevices();
            AvailableDevices.Clear();
            foreach (var device in devicesModel.SystemDevices.SystemDevices)
            {
                AvailableDevices.Add(device);
            }
        }

        private void OnIgnoreBoolChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EditableBoolViewModel.Value))
            {
                OnPropertyChanged(nameof(IsExpanderEnabled));
            }
        }

        private async Task DeleteWithAnimation()
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

                if (confirmed && AnimatedDeleteCallback != null)
                {
                    await AnimatedDeleteCallback(this);
                }
            }
            finally
            {
                isDeleting = false;
            }
        }

        public void DeleteSelf()
        {
            DevicesBE.TryRemoveElement(DeviceBE);
        }

    }
}