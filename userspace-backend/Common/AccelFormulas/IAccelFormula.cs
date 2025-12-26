namespace userspace_backend.Common.AccelFormulas
{
    /// <summary>
    /// Interface for acceleration formula computation.
    /// Returns sensitivity multiplier for a given input speed.
    /// </summary>
    public interface IAccelFormula
    {
        /// <summary>
        /// Calculate the sensitivity multiplier at the given input speed.
        /// </summary>
        /// <param name="speed">Input speed (counts/ms)</param>
        /// <returns>Sensitivity multiplier (1.0 = no change)</returns>
        double Calculate(double speed);
    }
}
