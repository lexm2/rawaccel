using System;
using System.Collections.Generic;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend.Common
{
    /// <summary>
    /// Result of LUT computation.
    /// </summary>
    public class LutResult
    {
        /// <summary>
        /// Interleaved (x, y) pairs.
        /// </summary>
        public float[] Data { get; set; } = Array.Empty<float>();

        /// <summary>
        /// Total number of floats (2 * point_count).
        /// </summary>
        public int Length { get; set; }
    }

    /// <summary>
    /// Interface for LUT computation.
    /// </summary>
    public interface ILutComputer
    {
        LutResult ComputeLut(IAccelFormula formula, double minSpeed = 0.05, double maxSpeed = 200.0);
    }

    /// <summary>
    /// Computes lookup tables from acceleration formulas using adaptive curvature-based sampling.
    /// Places more points where the curve bends most, fewer where it's flat.
    /// </summary>
    public class LutComputer : ILutComputer
    {
        private const int MaxPoints = 256;
        private const int SampleCount = 1000;

        public LutResult ComputeLut(IAccelFormula formula, double minSpeed = 0.05, double maxSpeed = 200.0)
        {
            return ComputeAdaptiveLut(formula, minSpeed, maxSpeed);
        }

        private LutResult ComputeAdaptiveLut(IAccelFormula formula, double minSpeed, double maxSpeed)
        {
            double h = (maxSpeed - minSpeed) / SampleCount;

            // Step 1: Compute geometric curvature at all sample points
            var curvatures = new double[SampleCount];
            double maxCurvature = 0;

            for (int i = 0; i < SampleCount; i++)
            {
                double x = minSpeed + i * h;
                curvatures[i] = GeometricCurvature(formula, x, h);
                maxCurvature = Math.Max(maxCurvature, curvatures[i]);
            }

            // Step 2: If curve is linear (no curvature), return just 2 points
            if (maxCurvature < 1e-9)
            {
                return new LutResult
                {
                    Data = new[]
                    {
                        (float)minSpeed, (float)formula.Calculate(minSpeed),
                        (float)maxSpeed, (float)formula.Calculate(maxSpeed)
                    },
                    Length = 4
                };
            }

            // Step 3: Compute cumulative curvature (integral)
            var cumulative = new double[SampleCount];
            cumulative[0] = curvatures[0];
            for (int i = 1; i < SampleCount; i++)
            {
                cumulative[i] = cumulative[i - 1] + curvatures[i];
            }
            double totalCurvature = cumulative[SampleCount - 1];

            // Step 4: Distribute points proportionally to curvature
            var points = new List<(double x, double y)>
            {
                (minSpeed, formula.Calculate(minSpeed))
            };

            double curvaturePerPoint = totalCurvature / (MaxPoints - 1);
            double targetCurvature = curvaturePerPoint;

            for (int i = 0; i < SampleCount && points.Count < MaxPoints - 1; i++)
            {
                if (cumulative[i] >= targetCurvature)
                {
                    double x = minSpeed + i * h;
                    points.Add((x, formula.Calculate(x)));
                    targetCurvature += curvaturePerPoint;
                }
            }

            points.Add((maxSpeed, formula.Calculate(maxSpeed)));

            // Convert to flat array
            var data = new float[points.Count * 2];
            for (int i = 0; i < points.Count; i++)
            {
                data[i * 2] = (float)points[i].x;
                data[i * 2 + 1] = (float)points[i].y;
            }

            return new LutResult { Data = data, Length = data.Length };
        }

        /// <summary>
        /// Compute geometric curvature: κ = |f''| / (1 + f'²)^(3/2)
        /// </summary>
        private double GeometricCurvature(IAccelFormula f, double x, double h)
        {
            // Ensure we don't go below minimum speed
            double xMinus = Math.Max(x - h, 0.01);
            double xPlus = x + h;

            double fMinus = f.Calculate(xMinus);
            double fX = f.Calculate(x);
            double fPlus = f.Calculate(xPlus);

            // f'(x) ≈ (f(x+h) - f(x-h)) / 2h
            double fp = (fPlus - fMinus) / (2 * h);

            // f''(x) ≈ (f(x+h) - 2f(x) + f(x-h)) / h²
            double fpp = (fPlus - 2 * fX + fMinus) / (h * h);

            // κ = |f''| / (1 + f'²)^(3/2)
            double denom = Math.Pow(1 + fp * fp, 1.5);

            if (denom < 1e-10) return 0;

            return Math.Abs(fpp) / denom;
        }
    }
}
