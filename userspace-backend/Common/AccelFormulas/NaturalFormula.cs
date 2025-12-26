using System;

namespace userspace_backend.Common.AccelFormulas
{
    /// <summary>
    /// Natural (vanishing difference) acceleration formula.
    /// Uses exponential decay approach.
    /// </summary>
    public class NaturalFormula : IAccelFormula
    {
        private readonly double _offset;
        private readonly double _accel;
        private readonly double _limit;
        private readonly double _constant;
        private readonly bool _isGain;

        public NaturalFormula(
            double decayRate,
            double inputOffset,
            double limit,
            bool gain)
        {
            _offset = inputOffset;
            _limit = limit - 1;
            _accel = decayRate / Math.Abs(_limit);
            _isGain = gain;
            _constant = -_limit / _accel;
        }

        public double Calculate(double x)
        {
            if (x <= _offset) return 1;

            double offsetX = _offset - x;
            double decay = Math.Exp(_accel * offsetX);

            if (_isGain)
            {
                double output = _limit * (decay / _accel - offsetX) + _constant;
                return output / x + 1;
            }
            else
            {
                return _limit * (1 - (_offset - decay * offsetX) / x) + 1;
            }
        }
    }
}
