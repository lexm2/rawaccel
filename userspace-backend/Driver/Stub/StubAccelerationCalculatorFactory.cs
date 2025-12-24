using userspace_backend.Model;

namespace userspace_backend.Driver.Stub
{
    /// <summary>
    /// Stub factory for non-Windows platforms.
    /// Creates stub calculators that return input unchanged.
    /// </summary>
    public class StubAccelerationCalculatorFactory : IAccelerationCalculatorFactory
    {
        public IAccelerationCalculator Create(IProfileModel profile)
        {
            return new StubAccelerationCalculator();
        }
    }
}
