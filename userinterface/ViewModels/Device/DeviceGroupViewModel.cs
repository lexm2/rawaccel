using System.Diagnostics;
using System.Windows.Input;
using userinterface.Commands;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Device
{
    public partial class DeviceGroupViewModel : ViewModelBase
    {
        public DeviceGroupViewModel(string deviceGroupName, BE.DeviceGroups deviceGroupsBE, bool isDefault = false)
        {
            DeviceGroupName = deviceGroupName;
            DeviceGroupsBE = deviceGroupsBE;
            IsDefaultGroup = isDefault;

            DeleteCommand = new RelayCommand(
                () => DeleteSelf());
        }

        public string DeviceGroupName { get; }

        protected BE.DeviceGroups DeviceGroupsBE { get; }

        public bool IsDefaultGroup { get; }

        public ICommand DeleteCommand { get; }

        public void DeleteSelf()
        {
            bool success = DeviceGroupsBE.RemoveDeviceGroup(DeviceGroupName);
            Debug.Assert(success);
        }
    }
}