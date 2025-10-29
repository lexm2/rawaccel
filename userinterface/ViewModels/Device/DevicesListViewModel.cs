using System;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Windows.Input;
using userinterface.Commands;
using userinterface.Services;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Device
{
    public partial class DevicesListViewModel : ViewModelBase, IDisposable
    {
        private bool disposed = false;
        private readonly IModalService modalService;
        private readonly ILocalizationService localizationService;

        public DevicesListViewModel(BE.IDevicesModel devicesBE, IModalService modalService, ILocalizationService localizationService)
        {
            DevicesBE = devicesBE;
            this.modalService = modalService;
            this.localizationService = localizationService;
            DeviceViews = [];
            UpdateDeviceViews();
            ((INotifyCollectionChanged)DevicesBE.Elements).CollectionChanged += DevicesCollectionChanged;

            AddDeviceCommand = new RelayCommand(
                () => TryAddDevice());
        }

        protected BE.IDevicesModel DevicesBE { get; }

        public ReadOnlyObservableCollection<BE.IDeviceModel> Devices => DevicesBE.Elements;

        public ObservableCollection<DeviceViewModel> DeviceViews { get; }

        public ICommand AddDeviceCommand { get; }

        private void DevicesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            switch (e.Action)
            {
                case NotifyCollectionChangedAction.Add:
                    if (e.NewItems != null)
                    {
                        foreach (BE.IDeviceModel device in e.NewItems)
                        {
                            int index = DevicesBE.Elements.IndexOf(device);
                            bool isDefault = index == 0;
                            var deviceViewModel = new DeviceViewModel(device, DevicesBE, modalService, localizationService, isDefault);
                            DeviceViews.Insert(index, deviceViewModel);
                        }
                    }
                    break;

                case NotifyCollectionChangedAction.Remove:
                    if (e.OldItems != null && e.OldStartingIndex >= 0)
                    {
                        for (int i = 0; i < e.OldItems.Count; i++)
                        {
                            DeviceViews.RemoveAt(e.OldStartingIndex);
                        }
                    }
                    break;

                case NotifyCollectionChangedAction.Reset:
                case NotifyCollectionChangedAction.Replace:
                case NotifyCollectionChangedAction.Move:
                default:
                    UpdateDeviceViews();
                    break;
            }
        }

        public void UpdateDeviceViews()
        {
            DeviceViews.Clear();

            for (int i = 0; i < DevicesBE.Elements.Count; i++)
            {
                var device = DevicesBE.Elements[i];
                bool isDefault = i == 0;
                DeviceViews.Add(new DeviceViewModel(device, DevicesBE, modalService, localizationService, isDefault));
            }
        }

        public bool TryAddDevice() => DevicesBE.TryAddNewDefault();

        public void Dispose()
        {
            if (disposed)
                return;

            ((INotifyCollectionChanged)DevicesBE.Elements).CollectionChanged -= DevicesCollectionChanged;

            DeviceViews.Clear();

            disposed = true;
            GC.SuppressFinalize(this);
        }
    }
}