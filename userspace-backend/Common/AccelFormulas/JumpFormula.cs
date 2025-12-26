using System;

namespace userspace_backend.Common.AccelFormulas
{
    /// <summary>
    /// Jump (step) acceleration formula.
    /// Provides a step change in sensitivity at a specified input speed.
    /// </summary>
    public class JumpFormula : IAccelFormula
    {
        private const double SmoothScale = 2 * Math.PI;

        private readonly double _stepX;
        private readonly double _stepY;
        private readonly double _smoothRate;
        private readonly double _constant;
        private readonly bool _isGain;

        public JumpFormula(
            double input,
            double output,
            double smooth,
            bool gain)
        {
            _stepX = input;
            _stepY = output - 1;
            _isGain = gain;

            double rateInverse = smooth * _stepX;
            _smoothRate = rateInverse < 1 ? 0 : SmoothScale / rateInverse;

            if (gain)
            {
                _constant = -SmoothAntideriv(0);
            }
        }

        public double Calculate(double x)
        {
            if (_isGain)
            {
                if (x <= 0) return 1;

                if (IsSmooth())
                {
                    return 1 + (SmoothAntideriv(x) + _constant) / x;
                }

                if (x < _stepX) return 1;
                return 1 + _stepY * (x - _stepX) / x;
            }
            else
            {
                if (IsSmooth())
                {
                    return Smooth(x) + 1;
                }

                if (x < _stepX) return 1;
                return 1 + _stepY;
            }
        }

        private bool IsSmooth() => _smoothRate != 0;

        private double Decay(double x)
        {
            return Math.Exp(_smoothRate * (_stepX - x));
        }

        private double Smooth(double x)
        {
            return _stepY / (1 + Decay(x));
        }

        private double SmoothAntideriv(double x)
        {
            return _stepY * (x + Math.Log(1 + Decay(x)) / _smoothRate);
        }
    }
}
