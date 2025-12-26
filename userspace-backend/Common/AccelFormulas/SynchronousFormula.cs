using System;

namespace userspace_backend.Common.AccelFormulas
{
    /// <summary>
    /// Synchronous (log-log sigmoid) acceleration formula.
    /// Provides smooth S-curve acceleration in log-log space.
    /// </summary>
    public class SynchronousFormula : IAccelFormula
    {
        private readonly double _logMotivity;
        private readonly double _gammaConst;
        private readonly double _logSyncSpeed;
        private readonly double _syncSpeed;
        private readonly double _sharpness;
        private readonly double _sharpnessRecip;
        private readonly bool _useLinearClamp;
        private readonly double _minimumSens;
        private readonly double _maximumSens;
        private readonly bool _isGain;

        // For gain mode integration
        private readonly double[]? _lutData;
        private readonly double _xStart;

        public SynchronousFormula(
            double syncSpeed,
            double motivity,
            double gamma,
            double smoothness,
            bool gain)
        {
            _syncSpeed = syncSpeed;
            _logMotivity = Math.Log(motivity);
            _gammaConst = gamma / _logMotivity;
            _logSyncSpeed = Math.Log(syncSpeed);
            _sharpness = smoothness == 0 ? 16 : 0.5 / smoothness;
            _sharpnessRecip = 1 / _sharpness;
            _useLinearClamp = _sharpness >= 16;
            _minimumSens = 1 / motivity;
            _maximumSens = motivity;
            _isGain = gain;

            if (gain)
            {
                // Pre-compute LUT for gain mode (numerical integration)
                _xStart = Math.Pow(2, -3);
                _lutData = ComputeGainLut();
            }
        }

        public double Calculate(double x)
        {
            if (_isGain)
            {
                return CalculateGain(x);
            }
            return CalculateLegacy(x);
        }

        private double CalculateLegacy(double x)
        {
            if (_useLinearClamp)
            {
                double logSpace = _gammaConst * (Math.Log(x) - _logSyncSpeed);

                if (logSpace < -1) return _minimumSens;
                if (logSpace > 1) return _maximumSens;

                return Math.Exp(logSpace * _logMotivity);
            }

            if (x == _syncSpeed) return 1.0;

            double logX = Math.Log(x);
            double logDiff = logX - _logSyncSpeed;

            if (logDiff > 0)
            {
                double logSpace = _gammaConst * logDiff;
                double exponent = Math.Pow(Math.Tanh(Math.Pow(logSpace, _sharpness)), _sharpnessRecip);
                return Math.Exp(exponent * _logMotivity);
            }
            else
            {
                double logSpace = -_gammaConst * logDiff;
                double exponent = -Math.Pow(Math.Tanh(Math.Pow(logSpace, _sharpness)), _sharpnessRecip);
                return Math.Exp(exponent * _logMotivity);
            }
        }

        private double CalculateGain(double x)
        {
            if (_lutData == null || x <= 0) return 1;

            // Use numerical integration result from LUT
            int rangeStart = -3;
            int rangeStop = 9;
            int rangeNum = 8;

            int e = Math.Min((int)Math.Floor(Math.Log2(x)), rangeStop - 1);

            if (e >= rangeStart)
            {
                int idxIntLogPart = e - rangeStart;
                double idxFracLinPart = x / Math.Pow(2, e) - 1;
                double idxF = rangeNum * (idxIntLogPart + idxFracLinPart);

                int idx = Math.Min((int)idxF, _lutData.Length - 2);

                if (idx >= 0 && idx < _lutData.Length - 1)
                {
                    double y = Lerp(_lutData[idx], _lutData[idx + 1], idxF - idx);
                    return y / x;
                }
            }

            return _lutData[0] / _xStart;
        }

        private double[] ComputeGainLut()
        {
            // Range: 2^-3 to 2^9 with 8 samples per octave
            int rangeStart = -3;
            int rangeStop = 9;
            int rangeNum = 8;
            int size = (rangeStop - rangeStart) * rangeNum;

            var data = new double[size];
            double sum = 0;
            double prevX = 0;

            int i = 0;
            for (int e = rangeStart; e < rangeStop; e++)
            {
                double baseVal = Math.Pow(2, e);
                for (int n = 0; n < rangeNum; n++)
                {
                    double x = baseVal * (1 + (double)n / rangeNum);

                    // Simple trapezoidal integration
                    if (prevX > 0)
                    {
                        double interval = x - prevX;
                        sum += CalculateLegacy(x) * interval;
                    }

                    data[i++] = sum;
                    prevX = x;
                }
            }

            return data;
        }

        private static double Lerp(double a, double b, double t)
        {
            return a + (b - a) * t;
        }
    }
}
