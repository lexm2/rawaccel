using System;
using userinterface.ViewModels.Device;
using userinterface.ViewModels.Mapping;
using userinterface.ViewModels.Profile;
using BE = userspace_backend.Model;

namespace userinterface.Services
{
    public interface IViewModelFactory
    {
        ProfileViewModel CreateProfileViewModel(BE.IProfileModel profileModel);
        ProfileSettingsViewModel CreateProfileSettingsViewModel(BE.IProfileModel profileModel);
        ProfileChartViewModel CreateProfileChartViewModel(BE.IProfileModel profileModel);
        MappingViewModel CreateMappingViewModel(BE.MappingModel mappingModel, BE.MappingsModel mappingsModel, bool isActive, Action<MappingViewModel> onActivationRequested);
    }
}