using userspace_backend.Model;

namespace userspace_backend.Driver
{
    /// <summary>
    /// Factory for creating acceleration calculators from profile models.
    /// </summary>
    public interface IAccelerationCalculatorFactory
    {
        /// <summary>
        /// Creates an acceleration calculator for the given profile.
        /// </summary>
        /// <param name="profile">The profile model to create a calculator for</param>
        /// <returns>An acceleration calculator configured for the profile</returns>
        IAccelerationCalculator Create(IProfileModel profile);
    }
}
