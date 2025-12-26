using System;

namespace userspace_backend.Common.AccelFormulas
{
    /// <summary>
    /// Power acceleration formula.
    /// Implements: (scale * x)^exponent + constant/x
    /// </summary>
    public class PowerFormula : IAccelFormula
    {
        private readonly double _scale;
        private readonly double _exponent;
        private readonly double _offsetX;
        private readonly double _offsetY;
        private readonly double _constant;
        private readonly double _capX;
        private readonly double _capY;
        private readonly double _constantB;
        private readonly bool _isGain;

        public PowerFormula(
            double scale,
            double exponent,
            double outputOffset,
            double capY,
            bool gain)
        {
            _scale = scale;
            _exponent = exponent;
            _isGain = gain;

            // Calculate offset from output_offset (gain mode calculation)
            _offsetX = GainInverse(outputOffset, exponent, scale);
            _offsetY = outputOffset;
            _constant = _offsetX * _offsetY * exponent / (exponent + 1);

            // Cap handling (output cap mode)
            if (capY > 0)
            {
                _capY = capY;

                if (gain)
                {
                    _capX = GainInverse(capY, exponent, scale);
                    _constantB = IntegrationConstant(_capX, capY, BaseFn(_capX));
                }
                else
                {
                    _capX = double.MaxValue;
                }
            }
            else
            {
                _capX = double.MaxValue;
                _capY = double.MaxValue;
            }
        }

        public double Calculate(double speed)
        {
            if (_isGain)
            {
                if (speed < _capX)
                {
                    return BaseFn(speed);
                }
                else
                {
                    return _capY + _constantB / speed;
                }
            }
            else
            {
                return Math.Min(BaseFn(speed), _capY);
            }
        }

        private double BaseFn(double x)
        {
            if (x <= _offsetX)
            {
                return _offsetY;
            }
            return Math.Pow(_scale * x, _exponent) + _constant / x;
        }

        private static double GainInverse(double gain, double power, double scale)
        {
            if (gain <= 0 || scale <= 0) return 0;
            return Math.Pow(gain / (power + 1), 1 / power) / scale;
        }

        private static double IntegrationConstant(double input, double gain, double output)
        {
            return (output - gain) * input;
        }
    }
}
