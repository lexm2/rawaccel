using System;
using userinterface.Services;
using IBackEnd = userspace_backend.IBackEnd;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Device
{
    public partial class DevicesPageViewModel : ViewModelBase
    {
        private DevicesListViewModel? devicesList;
        private DeviceGroupsViewModel? deviceGroups;
        private readonly BE.DevicesModel devicesModel;
        private readonly IModalService modalService;

        public DevicesPageViewModel(IBackEnd backEnd, IModalService modalService)
        {
            devicesModel = backEnd?.Devices ?? throw new ArgumentNullException(nameof(backEnd));
            this.modalService = modalService;
        }

        public DevicesListViewModel DevicesList =>
            devicesList ??= new DevicesListViewModel(devicesModel, modalService);

        public DeviceGroupsViewModel DeviceGroups =>
            deviceGroups ??= new DeviceGroupsViewModel(devicesModel.DeviceGroups);

        protected BE.DevicesModel DevicesModel => devicesModel;
    }
}
