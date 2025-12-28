using userspace_backend.Driver.Types;
using userspace_backend.Model;

namespace userspace_backend.Platform.Windows
{
    /// <summary>
    /// Windows implementation that creates ManagedAccel-backed calculators.
    /// </summary>
    public class WindowsAccelerationCalculatorFactory : IAccelerationCalculatorFactory
    {
        public IAccelerationCalculator Create(IProfileModel profile)
        {
            // Cast to ProfileModel to access the concrete implementation
            if (profile is ProfileModel profileModel)
            {
                // Use shared DriverMapper then convert to wrapper type
                DriverProfile sharedProfile = DriverMapper.MapProfile(profileModel);
                Profile wrapperProfile = WrapperTypeConverter.ToWrapperProfile(sharedProfile);
                ManagedAccel accel = new ManagedAccel(wrapperProfile);
                return new WindowsAccelerationCalculator(accel);
            }

            // Fallback - create with default profile
            return new WindowsAccelerationCalculator(new ManagedAccel());
        }
    }
}
