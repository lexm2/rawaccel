using System;

namespace userspace_backend.Common.AccelFormulas
{
    /// <summary>
    /// Classic (polynomial) acceleration formula.
    /// Implements: accel * (x - offset)^exponent / x
    /// </summary>
    public class ClassicFormula : IAccelFormula
    {
        private readonly double _accelRaised;
        private readonly double _inputOffset;
        private readonly double _exponent;
        private readonly double _capY;
        private readonly double _capX;
        private readonly double _constant;
        private readonly double _sign;
        private readonly bool _isGain;

        public ClassicFormula(
            double acceleration,
            double exponent,
            double inputOffset,
            double capY,
            bool gain)
        {
            _inputOffset = inputOffset;
            _exponent = exponent;
            _isGain = gain;
            _sign = 1;

            // Cap mode is always "output" in our simplified model
            _accelRaised = Math.Pow(acceleration, exponent - 1);

            if (capY > 0)
            {
                _capY = capY - 1;

                if (_capY < 0)
                {
                    _capY = -_capY;
                    _sign = -1;
                }

                if (gain)
                {
                    if (_capY == 0)
                    {
                        _capX = 0;
                    }
                    else
                    {
                        _capX = GainInverse(_capY, acceleration, exponent, inputOffset);
                        _constant = (BaseFn(_capX) - _capY) * _capX;
                    }
                }
            }
            else
            {
                _capY = double.MaxValue;
                _capX = double.MaxValue;
            }
        }

        public double Calculate(double x)
        {
            if (x <= _inputOffset) return 1;

            double output;

            if (_isGain)
            {
                if (x < _capX)
                {
                    output = BaseFn(x);
                }
                else
                {
                    output = _constant / x + _capY;
                }
            }
            else
            {
                output = Math.Min(BaseFn(x), _capY);
            }

            return _sign * output + 1;
        }

        private double BaseFn(double x)
        {
            return _accelRaised * Math.Pow(x - _inputOffset, _exponent) / x;
        }

        private static double GainInverse(double y, double accel, double power, double offset)
        {
            return (accel * offset + Math.Pow(y / power, 1 / (power - 1))) / accel;
        }
    }
}
