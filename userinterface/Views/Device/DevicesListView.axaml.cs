using System;
using System.Collections.Specialized;
using Avalonia.Controls;
using Avalonia.Threading;
using userinterface.ViewModels.Device;

namespace userinterface.Views.Device;

public partial class DevicesListView : UserControl
{
    private DevicesListViewModel? viewModel;

    public DevicesListView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    public void DeleteDevice(DeviceViewModel deviceViewModel)
    {
        deviceViewModel.DeleteSelf();
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is DevicesListViewModel vm)
        {
            if (viewModel != null)
            {
                viewModel.DeviceViews.CollectionChanged -= OnDevicesCollectionChanged;
            }

            viewModel = vm;
            vm.DeviceViews.CollectionChanged += OnDevicesCollectionChanged;
        }
    }

    private void OnDevicesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        // No-op - list updates automatically via binding
    }
}
