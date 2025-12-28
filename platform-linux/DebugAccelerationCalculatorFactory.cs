using userspace_backend.Driver;
using userspace_backend.Driver.Types;
using userspace_backend.Model;

namespace userspace_backend.Platform.Linux
{
    /// <summary>
    /// Debug factory that logs profile data when creating calculators.
    /// Creates debug calculators that return input unchanged.
    /// </summary>
    public class DebugAccelerationCalculatorFactory : IAccelerationCalculatorFactory
    {
        public IAccelerationCalculator Create(IProfileModel profile)
        {
            DebugLogger.LogHeader("CREATE ACCELERATION CALCULATOR");

            if (profile is ProfileModel profileModel)
            {
                DriverProfile driverProfile = DriverMapper.MapProfile(profileModel);
                DebugLogger.LogJson("Profile", driverProfile);
            }
            else
            {
                DebugLogger.Log($"Profile type: {profile?.GetType().Name ?? "null"}");
            }

            return new DebugAccelerationCalculator();
        }
    }
}
