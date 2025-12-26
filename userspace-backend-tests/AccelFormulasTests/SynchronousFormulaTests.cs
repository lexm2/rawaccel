using Microsoft.VisualStudio.TestTools.UnitTesting;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend_tests.AccelFormulasTests
{
    [TestClass]
    public class SynchronousFormulaTests
    {
        private const double Tolerance = 0.001;

        [TestMethod]
        public void Calculate_AtSyncSpeed_ReturnsOne()
        {
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            double result = formula.Calculate(10.0);
            Assert.AreEqual(1.0, result, Tolerance, "Output at sync speed should be 1.0");
        }

        [TestMethod]
        public void Calculate_BelowSyncSpeed_SensitivityBelowOne()
        {
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            double result = formula.Calculate(1.0);
            Assert.IsTrue(result < 1.0, $"Below sync speed, sensitivity should be < 1, got {result}");
        }

        [TestMethod]
        public void Calculate_AboveSyncSpeed_SensitivityAboveOne()
        {
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            double result = formula.Calculate(50.0);
            Assert.IsTrue(result > 1.0, $"Above sync speed, sensitivity should be > 1, got {result}");
        }

        [TestMethod]
        public void Calculate_MotivityBounds_MinimumSensitivity()
        {
            double motivity = 2.0;
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: motivity,
                gamma: 1.0,
                smoothness: 0.0, // Linear clamp mode (sharpness >= 16)
                gain: false);

            // At very low speeds, sensitivity should approach 1/motivity
            double veryLowSpeedResult = formula.Calculate(0.01);
            double expectedMin = 1.0 / motivity;

            Assert.IsTrue(veryLowSpeedResult >= expectedMin - Tolerance,
                $"Minimum sensitivity should be >= {expectedMin}, got {veryLowSpeedResult}");
        }

        [TestMethod]
        public void Calculate_MotivityBounds_MaximumSensitivity()
        {
            double motivity = 2.0;
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: motivity,
                gamma: 1.0,
                smoothness: 0.0, // Linear clamp mode
                gain: false);

            // At very high speeds, sensitivity should approach motivity
            double veryHighSpeedResult = formula.Calculate(10000.0);

            Assert.IsTrue(veryHighSpeedResult <= motivity + Tolerance,
                $"Maximum sensitivity should be <= {motivity}, got {veryHighSpeedResult}");
        }

        [TestMethod]
        public void Calculate_GammaEffect_ControlsCurveWidth()
        {
            var narrowGamma = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 2.0,  // Narrow transition
                smoothness: 0.5,
                gain: false);

            var wideGamma = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 0.5,  // Wide transition
                smoothness: 0.5,
                gain: false);

            // At same distance from sync speed, narrow gamma should be further from 1.0
            double narrowResult = narrowGamma.Calculate(20.0);
            double wideResult = wideGamma.Calculate(20.0);

            Assert.IsTrue(narrowResult > wideResult,
                $"Higher gamma should reach higher sensitivity sooner: {narrowResult} vs {wideResult}");
        }

        [TestMethod]
        public void Calculate_GainMode_DifferentBehavior()
        {
            var legacyFormula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            var gainFormula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: true);

            // Both should work, but gain mode uses numerical integration
            double legacyResult = legacyFormula.Calculate(20.0);
            double gainResult = gainFormula.Calculate(20.0);

            Assert.IsTrue(legacyResult > 1.0, "Legacy above sync speed should be > 1");
            Assert.IsTrue(gainResult > 0, "Gain mode should produce positive output");
        }

        [TestMethod]
        public void Calculate_SmoothnessZero_LinearClampMode()
        {
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 1.0,
                smoothness: 0.0, // Enables linear clamp mode
                gain: false);

            // In linear clamp mode, output is clamped to [1/motivity, motivity]
            double lowResult = formula.Calculate(0.1);
            double highResult = formula.Calculate(1000.0);

            Assert.AreEqual(0.5, lowResult, Tolerance, "Low speed should clamp to 1/motivity");
            Assert.AreEqual(2.0, highResult, Tolerance, "High speed should clamp to motivity");
        }

        [TestMethod]
        public void Calculate_OutputAlwaysPositive()
        {
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 2.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            double[] testSpeeds = { 0.1, 0.5, 1.0, 5.0, 10.0, 20.0, 50.0, 100.0 };

            foreach (double speed in testSpeeds)
            {
                double result = formula.Calculate(speed);
                Assert.IsTrue(result > 0, $"Output at speed {speed} should be positive, got {result}");
            }
        }

        [TestMethod]
        public void Calculate_SCurveShape_MonotonicIncrease()
        {
            var formula = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 3.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            double prev = formula.Calculate(0.1);
            for (double x = 0.5; x <= 100.0; x += 2.0)
            {
                double current = formula.Calculate(x);
                Assert.IsTrue(current >= prev - Tolerance,
                    $"S-curve should be monotonic: {current} should be >= {prev} at speed {x}");
                prev = current;
            }
        }

        [TestMethod]
        public void Calculate_HigherMotivity_WiderRange()
        {
            var lowMotivity = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 1.5,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            var highMotivity = new SynchronousFormula(
                syncSpeed: 10.0,
                motivity: 4.0,
                gamma: 1.0,
                smoothness: 0.5,
                gain: false);

            // At high speed, higher motivity should produce higher sensitivity
            double lowResult = lowMotivity.Calculate(100.0);
            double highResult = highMotivity.Calculate(100.0);

            Assert.IsTrue(highResult > lowResult,
                $"Higher motivity should allow higher max sensitivity: {highResult} vs {lowResult}");

            // At low speed, higher motivity should produce lower sensitivity
            double lowResultLow = lowMotivity.Calculate(0.5);
            double highResultLow = highMotivity.Calculate(0.5);

            Assert.IsTrue(highResultLow < lowResultLow,
                $"Higher motivity should allow lower min sensitivity: {highResultLow} vs {lowResultLow}");
        }
    }
}
