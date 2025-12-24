#if WINDOWS
using userspace_backend.Common;
using userspace_backend.Model;

namespace userspace_backend.Driver.Windows
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
                Profile driverProfile = DriverHelpers.MapProfileModelToDriver(profileModel);
                ManagedAccel accel = new ManagedAccel(driverProfile);
                return new WindowsAccelerationCalculator(accel);
            }

            // Fallback - create with default profile
            return new WindowsAccelerationCalculator(new ManagedAccel());
        }
    }
}
#endif
