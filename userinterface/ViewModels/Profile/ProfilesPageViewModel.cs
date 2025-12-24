using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using userinterface.Interfaces;
using userinterface.Services;
using IBackEnd = userspace_backend.IBackEnd;
using BE = userspace_backend.Model;

namespace userinterface.ViewModels.Profile
{
    public partial class ProfilesPageViewModel : ViewModelBase, IAsyncInitializable
    {
        [ObservableProperty]
        public ProfileViewModel? selectedProfileView;

        private readonly INotificationService notificationService;
        private readonly BE.IProfilesModel profilesModel;
        private readonly ProfileListViewModel profileListView;
        private readonly IViewModelFactory viewModelFactory;

        public ProfilesPageViewModel(
            INotificationService notificationService,
            IBackEnd backEnd,
            ProfileListViewModel profileListView,
            IViewModelFactory viewModelFactory)
        {
            this.notificationService = notificationService ?? throw new ArgumentNullException(nameof(notificationService));
            this.profilesModel = backEnd?.Profiles ?? throw new ArgumentNullException(nameof(backEnd));
            this.profileListView = profileListView ?? throw new ArgumentNullException(nameof(profileListView));
            this.viewModelFactory = viewModelFactory ?? throw new ArgumentNullException(nameof(viewModelFactory));

            ProfileViewModels = [];
            UpdateProfileViewModels();

            profileListView.SelectedProfileChanged += OnProfileSelectionChanged;
        }

        private INotificationService NotificationService => notificationService;
        private BE.IProfilesModel ProfilesModel => profilesModel;
        public ProfileListViewModel ProfileListView => profileListView;

        private IEnumerable<BE.IProfileModel> ProfileModels => ProfilesModel.Profiles;

        protected ObservableCollection<ProfileViewModel> ProfileViewModels { get; }

        public bool IsInitialized { get; private set; } = true; // Already initialized in constructor

        public bool IsInitializing { get; private set; }

        public Task InitializeAsync()
        {
            if (IsInitializing || IsInitialized)
                return Task.CompletedTask;

            IsInitializing = true;

            UpdateProfileViewModels();

            IsInitializing = false;
            IsInitialized = true;

            return Task.CompletedTask;
        }


        public void UpdateCurrentProfile()
        {
            UpdateProfileViewModels();
            UpdateSelectedProfileView(ProfileListView.SelectedProfile);
        }

        private void UpdateSelectedProfileView(BE.IProfileModel? currentProfile)
        {
            if (currentProfile?.CurrentNameForDisplay != null)
            {
                SelectedProfileView = ProfileViewModels.FirstOrDefault(
                    p => string.Equals(p.CurrentName, currentProfile.CurrentNameForDisplay, StringComparison.InvariantCultureIgnoreCase))
                    ?? ProfileViewModels.FirstOrDefault();
            }
            else
            {
                SelectedProfileView = ProfileViewModels.FirstOrDefault();
            }
        }

        protected void UpdateProfileViewModels()
        {
            ProfileViewModels.Clear();
            
            foreach (var profileModelBE in ProfileModels)
            {
                var profileViewModel = viewModelFactory.CreateProfileViewModel(profileModelBE);
                ProfileViewModels.Add(profileViewModel);
            }
        }

        private void OnProfileSelectionChanged(BE.IProfileModel selectedProfile)
        {
            UpdateSelectedProfileView(selectedProfile);
        }
    }
}