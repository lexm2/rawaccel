using Microsoft.VisualStudio.TestTools.UnitTesting;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend_tests.AccelFormulasTests
{
    [TestClass]
    public class PowerFormulaTests
    {
        private const double Tolerance = 0.0001;

        [TestMethod]
        public void Calculate_AtOutputOffset_ReturnsOffsetValue()
        {
            var formula = new PowerFormula(
                scale: 0.1,
                exponent: 0.5,
                outputOffset: 1.5,
                capY: 0.0,
                gain: false);

            // At very low speeds (below offset point), should return the output offset
            double result = formula.Calculate(0.1);
            Assert.AreEqual(1.5, result, Tolerance, "Should return output offset at low speeds");
        }

        [TestMethod]
        public void Calculate_AboveOffset_IncreasesSensitivity()
        {
            var formula = new PowerFormula(
                scale: 0.1,
                exponent: 0.5,
                outputOffset: 1.0,
                capY: 0.0,
                gain: false);

            double lowSpeed = formula.Calculate(5.0);
            double midSpeed = formula.Calculate(20.0);
            double highSpeed = formula.Calculate(50.0);

            Assert.IsTrue(midSpeed >= lowSpeed, "Sensitivity should increase with speed");
            Assert.IsTrue(highSpeed >= midSpeed, "Sensitivity should continue increasing");
        }

        [TestMethod]
        public void Calculate_LegacyMode_CapsOutput()
        {
            var formula = new PowerFormula(
                scale: 0.5,  // High scale for fast growth
                exponent: 1.0,
                outputOffset: 1.0,
                capY: 3.0,
                gain: false);

            double highSpeedResult = formula.Calculate(100.0);

            Assert.IsTrue(highSpeedResult <= 3.0 + Tolerance,
                $"Output {highSpeedResult} should not exceed cap of 3.0");
        }

        [TestMethod]
        public void Calculate_GainMode_DifferentBehavior()
        {
            var legacyFormula = new PowerFormula(
                scale: 0.1,
                exponent: 0.5,
                outputOffset: 1.0,
                capY: 3.0,
                gain: false);

            var gainFormula = new PowerFormula(
                scale: 0.1,
                exponent: 0.5,
                outputOffset: 1.0,
                capY: 3.0,
                gain: true);

            // Both should start at output offset
            double legacyLow = legacyFormula.Calculate(0.1);
            double gainLow = gainFormula.Calculate(0.1);

            Assert.IsTrue(legacyLow >= 1.0, "Legacy should be at least offset value");
            Assert.IsTrue(gainLow >= 1.0, "Gain should be at least offset value");

            // At higher speeds, both should accelerate
            Assert.IsTrue(legacyFormula.Calculate(20.0) > legacyLow);
            Assert.IsTrue(gainFormula.Calculate(20.0) > gainLow);
        }

        [TestMethod]
        public void Calculate_HigherExponent_SteeperCurve()
        {
            var lowExponent = new PowerFormula(
                scale: 0.1,
                exponent: 0.3,
                outputOffset: 1.0,
                capY: 0.0,
                gain: false);

            var highExponent = new PowerFormula(
                scale: 0.1,
                exponent: 0.8,
                outputOffset: 1.0,
                capY: 0.0,
                gain: false);

            // At high speeds, higher exponent should produce larger output
            double lowResult = lowExponent.Calculate(50.0);
            double highResult = highExponent.Calculate(50.0);

            Assert.IsTrue(highResult > lowResult,
                $"Higher exponent should produce larger output: {highResult} vs {lowResult}");
        }

        [TestMethod]
        public void Calculate_HigherScale_FasterGrowth()
        {
            var lowScale = new PowerFormula(
                scale: 0.05,
                exponent: 0.5,
                outputOffset: 1.0,
                capY: 0.0,
                gain: false);

            var highScale = new PowerFormula(
                scale: 0.2,
                exponent: 0.5,
                outputOffset: 1.0,
                capY: 0.0,
                gain: false);

            double lowResult = lowScale.Calculate(30.0);
            double highResult = highScale.Calculate(30.0);

            Assert.IsTrue(highResult > lowResult,
                $"Higher scale should produce larger output: {highResult} vs {lowResult}");
        }

        [TestMethod]
        public void Calculate_OutputAlwaysPositive()
        {
            var formula = new PowerFormula(
                scale: 0.1,
                exponent: 0.5,
                outputOffset: 1.0,
                capY: 3.0,
                gain: false);

            for (double x = 0.1; x <= 100.0; x += 2.5)
            {
                double result = formula.Calculate(x);
                Assert.IsTrue(result > 0, $"Output at speed {x} should be positive, got {result}");
            }
        }

        [TestMethod]
        public void Calculate_Monotonicity_OutputNeverDecreases()
        {
            var formula = new PowerFormula(
                scale: 0.1,
                exponent: 0.5,
                outputOffset: 1.0,
                capY: 0.0,
                gain: false);

            double prev = formula.Calculate(0.1);
            for (double x = 0.5; x <= 50.0; x += 0.5)
            {
                double current = formula.Calculate(x);
                Assert.IsTrue(current >= prev - Tolerance,
                    $"Output at speed {x} ({current}) should be >= output at previous speed ({prev})");
                prev = current;
            }
        }

        [TestMethod]
        public void Calculate_VerySmallScale_NearOffset()
        {
            var formula = new PowerFormula(
                scale: 0.001,  // Very small but non-zero scale
                exponent: 0.5,
                outputOffset: 1.5,
                capY: 0.0,
                gain: false);

            // With very small scale, output should be close to offset at moderate speeds
            double result = formula.Calculate(5.0);
            Assert.IsTrue(result >= 1.5 - Tolerance, $"Output {result} should be near offset");
            Assert.IsTrue(result < 2.0, $"Output {result} should not grow quickly with small scale");
        }
    }
}
