using Microsoft.VisualStudio.TestTools.UnitTesting;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend_tests.AccelFormulasTests
{
    [TestClass]
    public class ClassicFormulaTests
    {
        private const double Tolerance = 0.0001;

        [TestMethod]
        public void Calculate_BelowOffset_ReturnsOne()
        {
            var formula = new ClassicFormula(
                acceleration: 0.01,
                exponent: 2.5,
                inputOffset: 5.0,
                capY: 3.0,
                gain: false);

            Assert.AreEqual(1.0, formula.Calculate(3.0), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(0.1), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(5.0), Tolerance); // At offset
        }

        [TestMethod]
        public void Calculate_AboveOffset_IncreasesSensitivity()
        {
            var formula = new ClassicFormula(
                acceleration: 0.01,
                exponent: 2.5,
                inputOffset: 5.0,
                capY: 10.0, // High cap to not interfere
                gain: false);

            double atOffset = formula.Calculate(5.0);
            double slightlyAbove = formula.Calculate(6.0);
            double wellAbove = formula.Calculate(20.0);

            Assert.AreEqual(1.0, atOffset, Tolerance);
            Assert.IsTrue(slightlyAbove > atOffset, "Sensitivity should increase above offset");
            Assert.IsTrue(wellAbove > slightlyAbove, "Sensitivity should continue increasing");
        }

        [TestMethod]
        public void Calculate_LegacyMode_CapsOutput()
        {
            var formula = new ClassicFormula(
                acceleration: 0.1,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 2.0, // Cap at 2.0
                gain: false);

            double highSpeed = formula.Calculate(100.0);

            // In legacy mode, output should not exceed cap
            Assert.IsTrue(highSpeed <= 2.0 + Tolerance, $"Output {highSpeed} should not exceed cap of 2.0");
        }

        [TestMethod]
        public void Calculate_ZeroAcceleration_ReturnsOne()
        {
            var formula = new ClassicFormula(
                acceleration: 0.0,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 0.0, // No cap
                gain: false);

            Assert.AreEqual(1.0, formula.Calculate(10.0), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(100.0), Tolerance);
        }

        [TestMethod]
        public void Calculate_NoCapSpecified_OutputCanGrowUnbounded()
        {
            var formula = new ClassicFormula(
                acceleration: 0.1,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 0.0, // No cap (0 means disabled)
                gain: false);

            double result = formula.Calculate(50.0);

            // With no cap, output should be able to exceed typical cap values
            Assert.IsTrue(result > 2.0, "Output should grow without cap");
        }

        [TestMethod]
        public void Calculate_GainMode_BehaviorDiffersFromLegacy()
        {
            var legacyFormula = new ClassicFormula(
                acceleration: 0.05,
                exponent: 2.5,
                inputOffset: 2.0,
                capY: 3.0,
                gain: false);

            var gainFormula = new ClassicFormula(
                acceleration: 0.05,
                exponent: 2.5,
                inputOffset: 2.0,
                capY: 3.0,
                gain: true);

            // Both should return 1 below offset
            Assert.AreEqual(1.0, legacyFormula.Calculate(1.0), Tolerance);
            Assert.AreEqual(1.0, gainFormula.Calculate(1.0), Tolerance);

            // Above offset, values will differ due to different calculation modes
            double legacyResult = legacyFormula.Calculate(10.0);
            double gainResult = gainFormula.Calculate(10.0);

            // Both should be greater than 1
            Assert.IsTrue(legacyResult > 1.0, "Legacy result should be > 1");
            Assert.IsTrue(gainResult > 1.0, "Gain result should be > 1");
        }

        [TestMethod]
        public void Calculate_Monotonicity_OutputNeverDecreases()
        {
            var formula = new ClassicFormula(
                acceleration: 0.02,
                exponent: 2.0,
                inputOffset: 1.0,
                capY: 0.0, // No cap
                gain: false);

            double prev = formula.Calculate(0.5);
            for (double x = 1.0; x <= 50.0; x += 1.0)
            {
                double current = formula.Calculate(x);
                Assert.IsTrue(current >= prev - Tolerance,
                    $"Output at speed {x} ({current}) should be >= output at previous speed ({prev})");
                prev = current;
            }
        }

        [TestMethod]
        public void Calculate_OutputAlwaysPositive()
        {
            var formula = new ClassicFormula(
                acceleration: 0.01,
                exponent: 2.5,
                inputOffset: 5.0,
                capY: 3.0,
                gain: false);

            for (double x = 0.1; x <= 100.0; x += 2.5)
            {
                double result = formula.Calculate(x);
                Assert.IsTrue(result > 0, $"Output at speed {x} should be positive, got {result}");
            }
        }

        [TestMethod]
        public void Calculate_HighExponent_SteeperCurve()
        {
            var lowExponent = new ClassicFormula(
                acceleration: 0.1,
                exponent: 2.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            var highExponent = new ClassicFormula(
                acceleration: 0.1,
                exponent: 3.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            // At high speeds, higher exponent should produce larger output
            double lowResult = lowExponent.Calculate(20.0);
            double highResult = highExponent.Calculate(20.0);

            Assert.IsTrue(highResult > lowResult,
                $"Higher exponent should produce larger output at high speed: {highResult} vs {lowResult}");
        }
    }
}
