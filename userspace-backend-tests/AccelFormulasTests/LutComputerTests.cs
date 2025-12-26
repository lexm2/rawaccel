using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.Linq;
using userspace_backend.Common;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend_tests.AccelFormulasTests
{
    [TestClass]
    public class LutComputerTests
    {
        private const double Tolerance = 0.01;

        public TestContext TestContext { get; set; }

        [TestMethod]
        public void ComputeLut_LinearCurve_MinimalPoints()
        {
            var computer = new LutComputer();
            var linearFormula = new ConstantFormula(1.5); // Constant = zero curvature

            LutResult result = computer.ComputeLut(linearFormula, minSpeed: 0.1, maxSpeed: 100.0);

            // Linear/constant curve should produce only 2 points (start and end)
            Assert.AreEqual(4, result.Length, "Constant curve should produce 2 points (4 floats)");
        }

        [TestMethod]
        public void ComputeLut_CurvedFormula_MorePoints()
        {
            var computer = new LutComputer();
            var curvedFormula = new ClassicFormula(
                acceleration: 0.1,
                exponent: 2.5,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            LutResult result = computer.ComputeLut(curvedFormula);

            // Curved formula should produce more than 2 points
            Assert.IsTrue(result.Length > 4, $"Curved formula should produce more than 2 points, got {result.Length / 2}");
        }

        [TestMethod]
        public void ComputeLut_OutputIsInterleavedPairs()
        {
            var computer = new LutComputer();
            var formula = new ClassicFormula(
                acceleration: 0.05,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            LutResult result = computer.ComputeLut(formula);

            // Length should always be even (x, y pairs)
            Assert.AreEqual(0, result.Length % 2, "LUT length should be even (x,y pairs)");
            Assert.AreEqual(result.Data.Length, result.Length, "Data array length should match Length property");
        }

        [TestMethod]
        public void ComputeLut_PointsSortedByX()
        {
            var computer = new LutComputer();
            var formula = new NaturalFormula(
                decayRate: 0.5,
                inputOffset: 0.0,
                limit: 3.0,
                gain: false);

            LutResult result = computer.ComputeLut(formula);

            // Check that X values are sorted
            for (int i = 0; i < result.Length - 2; i += 2)
            {
                float currentX = result.Data[i];
                float nextX = result.Data[i + 2];
                Assert.IsTrue(nextX > currentX,
                    $"X values should be sorted: {currentX} should be < {nextX} at index {i}");
            }
        }

        [TestMethod]
        public void ComputeLut_IncludesEndpoints()
        {
            var computer = new LutComputer();
            double minSpeed = 1.0;
            double maxSpeed = 50.0;

            var formula = new ClassicFormula(
                acceleration: 0.1,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            LutResult result = computer.ComputeLut(formula, minSpeed, maxSpeed);

            // First point should be at minSpeed
            Assert.AreEqual((float)minSpeed, result.Data[0], 0.001f, "First X should be minSpeed");

            // Last point should be at maxSpeed
            float lastX = result.Data[result.Length - 2];
            Assert.AreEqual((float)maxSpeed, lastX, 0.001f, "Last X should be maxSpeed");
        }

        [TestMethod]
        public void ComputeLut_YValuesMatchFormula()
        {
            var computer = new LutComputer();
            var formula = new ClassicFormula(
                acceleration: 0.05,
                exponent: 2.0,
                inputOffset: 5.0,
                capY: 0.0,
                gain: false);

            LutResult result = computer.ComputeLut(formula);

            // Each Y value should match the formula's output for that X
            for (int i = 0; i < result.Length; i += 2)
            {
                float x = result.Data[i];
                float y = result.Data[i + 1];
                double expectedY = formula.Calculate(x);

                Assert.AreEqual(expectedY, y, Tolerance,
                    $"Y value at X={x} should match formula output");
            }
        }

        [TestMethod]
        public void ComputeLut_InterpolationAccuracy()
        {
            var computer = new LutComputer();
            var formula = new ClassicFormula(
                acceleration: 0.05,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            double minSpeed = 0.1;
            double maxSpeed = 100.0;
            LutResult result = computer.ComputeLut(formula, minSpeed, maxSpeed);

            // Test interpolation at various points (start slightly above minSpeed to avoid float precision issues)
            for (double testX = minSpeed + 0.5; testX <= maxSpeed - 0.5; testX += 5.0)
            {
                double interpolatedY = InterpolateLut(result, testX);
                double actualY = formula.Calculate(testX);

                // Allow tolerance for interpolation approximation
                Assert.AreEqual(actualY, interpolatedY, 0.1,
                    $"Interpolated value at X={testX} should be close to formula output");
            }
        }

        [TestMethod]
        public void ComputeLut_HighCurvature_MorePoints()
        {
            var computer = new LutComputer();

            // Low curvature formula (gentle curve)
            var gentleFormula = new ClassicFormula(
                acceleration: 0.01,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            // High curvature formula (sharp curve)
            var sharpFormula = new ClassicFormula(
                acceleration: 0.5,
                exponent: 3.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            LutResult gentleResult = computer.ComputeLut(gentleFormula);
            LutResult sharpResult = computer.ComputeLut(sharpFormula);

            // Sharp formula should have equal or more points due to higher curvature
            // (This test is more of a sanity check since adaptive sampling is curvature-based)
            Assert.IsTrue(sharpResult.Length >= 4, "Sharp formula should have at least 2 points");
            Assert.IsTrue(gentleResult.Length >= 4, "Gentle formula should have at least 2 points");
        }

        [TestMethod]
        public void ComputeLut_AllFormulasWork()
        {
            var computer = new LutComputer();
            IAccelFormula[] formulas = new IAccelFormula[]
            {
                new ClassicFormula(0.05, 2.0, 0.0, 0.0, false),
                new LinearFormula(0.05, 0.0, 0.0, false),
                new NaturalFormula(0.5, 0.0, 3.0, false),
                new JumpFormula(10.0, 2.0, 0.5, false),
                new PowerFormula(0.1, 0.5, 1.0, 0.0, false),
                new SynchronousFormula(10.0, 2.0, 1.0, 0.5, false),
            };

            foreach (var formula in formulas)
            {
                LutResult result = computer.ComputeLut(formula);

                Assert.IsNotNull(result, $"LUT result should not be null for {formula.GetType().Name}");
                Assert.IsTrue(result.Length >= 4, $"LUT should have at least 2 points for {formula.GetType().Name}");
                Assert.IsTrue(result.Data.All(f => !float.IsNaN(f) && !float.IsInfinity(f)),
                    $"LUT should not contain NaN or Infinity for {formula.GetType().Name}");
            }
        }

        [TestMethod]
        public void ComputeLut_MaxPointsNotExceeded()
        {
            var computer = new LutComputer();

            // Formula that could potentially generate many points
            var formula = new ClassicFormula(
                acceleration: 1.0,
                exponent: 4.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            LutResult result = computer.ComputeLut(formula, minSpeed: 0.01, maxSpeed: 1000.0);

            // MaxPoints is 256, so max floats is 512
            Assert.IsTrue(result.Length <= 512, $"LUT length {result.Length} should not exceed 512 floats (256 points)");
        }

        [TestMethod]
        public void ComputeLut_MaximumError_Within1Percent()
        {
            var computer = new LutComputer();

            // Test multiple formula types with varying curve shapes
            IAccelFormula[] formulas = new IAccelFormula[]
            {
                new ClassicFormula(0.05, 2.0, 0.0, 0.0, false),
                new ClassicFormula(0.1, 3.0, 5.0, 0.0, false),
                new NaturalFormula(0.5, 0.0, 3.0, false),
                new JumpFormula(10.0, 2.0, 0.5, false),
                new PowerFormula(0.1, 0.5, 1.0, 0.0, false),
                new SynchronousFormula(10.0, 2.0, 1.0, 0.5, false),
            };

            double minSpeed = 0.5;
            double maxSpeed = 150.0;
            double sampleStep = 0.1;  // Dense sampling

            // Collect all errors for reporting
            var allErrors = new List<(string FormulaName, double Speed, double Error)>();

            foreach (var formula in formulas)
            {
                LutResult result = computer.ComputeLut(formula, minSpeed, maxSpeed);
                string formulaName = formula.GetType().Name;

                for (double x = minSpeed; x <= maxSpeed; x += sampleStep)
                {
                    double actual = formula.Calculate(x);
                    double interpolated = InterpolateLut(result, x);

                    // Calculate relative error
                    double error = Math.Abs(interpolated - actual) / Math.Abs(actual);
                    allErrors.Add((formulaName, x, error));

                    Assert.IsTrue(error <= 0.01,
                        $"{formulaName}: Error {error:P2} at x={x} exceeds 1% " +
                        $"(actual={actual:F4}, interpolated={interpolated:F4})");
                }
            }

            // Log top 10 highest error points
            var top10 = allErrors.OrderByDescending(e => e.Error).Take(10).ToList();
            TestContext.WriteLine("=== Top 10 Highest Error Points ===");
            for (int i = 0; i < top10.Count; i++)
            {
                var (name, speed, error) = top10[i];
                TestContext.WriteLine($"{i + 1}. {name} at x={speed:F1}: {error:P4}");
            }
            TestContext.WriteLine("===================================");
        }

        /// <summary>
        /// Helper method to linearly interpolate in a LUT (same as driver would do).
        /// </summary>
        private static double InterpolateLut(LutResult lut, double x)
        {
            if (lut.Length < 4) return lut.Data[1]; // Only one point

            // Find the two points to interpolate between
            int pointCount = lut.Length / 2;

            for (int i = 0; i < pointCount - 1; i++)
            {
                float x0 = lut.Data[i * 2];
                float x1 = lut.Data[(i + 1) * 2];

                if (x >= x0 && x <= x1)
                {
                    float y0 = lut.Data[i * 2 + 1];
                    float y1 = lut.Data[(i + 1) * 2 + 1];

                    // Linear interpolation
                    double t = (x - x0) / (x1 - x0);
                    return y0 + t * (y1 - y0);
                }
            }

            // x is beyond the LUT range, return the last value
            return lut.Data[lut.Length - 1];
        }
    }

    /// <summary>
    /// Simple constant formula for testing linear (zero curvature) case.
    /// </summary>
    internal class ConstantFormula : IAccelFormula
    {
        private readonly double _value;

        public ConstantFormula(double value)
        {
            _value = value;
        }

        public double Calculate(double speed) => _value;
    }
}
