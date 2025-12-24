namespace userspace_backend.Driver.Stub
{
    /// <summary>
    /// Stub acceleration calculator for non-Windows platforms.
    /// Returns input unchanged (1:1 acceleration).
    /// </summary>
    public class StubAccelerationCalculator : IAccelerationCalculator
    {
        public (double x, double y) Accelerate(double x, double y, double dpiFactor, double time)
        {
            // Return input unchanged - represents 1:1 acceleration (flat line on graph)
            return (x, y);
        }

        public IAccelerationCalculator CreateStatelessCopy()
        {
            // Stub is already stateless
            return this;
        }
    }
}
