using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Services;
using userinterface.ViewModels.Fields;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Device
{
    public partial class DeviceViewModel : ViewModelBase, IDisposable
    {
        public class DeviceDeleteConfirmedEventArgs : EventArgs
        {
            public DeviceViewModel Device { get; }

            public DeviceDeleteConfirmedEventArgs(DeviceViewModel device)
            {
                Device = device;
            }
        }

        private bool disposed = false;
        private readonly IModalService modalService;

        public event EventHandler<DeviceDeleteConfirmedEventArgs>? DeleteConfirmed;

        public DeviceViewModel(BE.IDeviceModel deviceBE, BE.IDevicesModel devicesBE, IModalService modalService, LocalizationService localizationService, bool isDefault = false)
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

                if (confirmed)
                {
                    DeleteConfirmed?.Invoke(this, new DeviceDeleteConfirmedEventArgs(this));
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

        public void Dispose()
        {
            if (disposed)
                return;

            IgnoreBool.PropertyChanged -= OnIgnoreBoolChanged;

            disposed = true;
            GC.SuppressFinalize(this);
        }

    }
}