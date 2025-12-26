namespace userspace_backend.Common.AccelFormulas
{
    /// <summary>
    /// Linear acceleration formula.
    /// Equivalent to Classic with exponent=2.
    /// </summary>
    public class LinearFormula : IAccelFormula
    {
        private readonly ClassicFormula _classic;

        public LinearFormula(
            double acceleration,
            double inputOffset,
            double capY,
            bool gain)
        {
            // Linear is Classic with exponent = 2
            _classic = new ClassicFormula(
                acceleration,
                exponent: 2,
                inputOffset,
                capY,
                gain);
        }

        public double Calculate(double speed)
        {
            return _classic.Calculate(speed);
        }
    }
}
