using RawAccel.Contracts;

namespace userspace_backend.Driver.Linux
{
    // Identity-pass IAccelEvaluator stub for Linux. The chart still draws
    // (every point reports input speed unchanged), but the actual curve
    // shape is not previewed until P/Invoke to a stripped libcommon.so
    // is wired up. That work is intentionally deferred: the apply path is
    // the higher-priority correctness target, and the agent's lookup-table
    // backend already runs the real common/ math in-kernel.
    public sealed class LinuxAccelEvaluator : IAccelEvaluator
    {
        public IAccelInstance CreateInstance(RawAccelProfile profile)
            => new IdentityInstance();

        private sealed class IdentityInstance : IAccelInstance
        {
            public (double x, double y) Accelerate(
                double x, double y, double dpiFactor, double timeMs)
                => (x, y);
        }
    }
}
