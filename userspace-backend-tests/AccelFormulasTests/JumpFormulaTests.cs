using Microsoft.VisualStudio.TestTools.UnitTesting;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend_tests.AccelFormulasTests
{
    [TestClass]
    public class JumpFormulaTests
    {
        private const double Tolerance = 0.0001;

        [TestMethod]
        public void Calculate_NoSmooth_StepBehavior()
        {
            var formula = new JumpFormula(
                input: 10.0,  // Step at 10 counts/ms
                output: 2.0,  // Jump to 2x sensitivity
                smooth: 0.0,  // No smoothing
                gain: false);

            // Below step point
            Assert.AreEqual(1.0, formula.Calculate(5.0), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(9.9), Tolerance);

            // At and above step point
            Assert.AreEqual(2.0, formula.Calculate(10.0), Tolerance);
            Assert.AreEqual(2.0, formula.Calculate(20.0), Tolerance);
        }

        [TestMethod]
        public void Calculate_WithSmooth_TransitionGradual()
        {
            var formula = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.5,  // Smoothing enabled
                gain: false);

            double wellBelow = formula.Calculate(2.0);
            double justBelow = formula.Calculate(9.0);
            double atStep = formula.Calculate(10.0);
            double justAbove = formula.Calculate(11.0);
            double wellAbove = formula.Calculate(20.0);

            // Should transition smoothly, not step
            Assert.IsTrue(wellBelow < justBelow, "Output should increase approaching step");
            Assert.IsTrue(justBelow < atStep, "Output should continue increasing");
            Assert.IsTrue(atStep < justAbove, "Output should continue increasing past step");

            // Far from step point, should approach limits
            Assert.IsTrue(wellBelow < 1.5, "Well below should be closer to 1.0");
            Assert.IsTrue(wellAbove > 1.5, "Well above should be closer to 2.0");
        }

        [TestMethod]
        public void Calculate_GainMode_NoSmooth_DifferentBehavior()
        {
            var legacyFormula = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.0,
                gain: false);

            var gainFormula = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.0,
                gain: true);

            // Below step, both should be 1
            Assert.AreEqual(1.0, legacyFormula.Calculate(5.0), Tolerance);
            Assert.AreEqual(1.0, gainFormula.Calculate(5.0), Tolerance);

            // Above step, gain mode behaves differently
            double legacyResult = legacyFormula.Calculate(20.0);
            double gainResult = gainFormula.Calculate(20.0);

            // Legacy should stay at output level
            Assert.AreEqual(2.0, legacyResult, Tolerance);

            // Gain mode integrates differently
            Assert.IsTrue(gainResult > 1.0, "Gain mode should show acceleration");
        }

        [TestMethod]
        public void Calculate_GainMode_WithSmooth()
        {
            var formula = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.5,
                gain: true);

            // Should produce smooth curve in gain mode too
            double low = formula.Calculate(2.0);
            double mid = formula.Calculate(10.0);
            double high = formula.Calculate(20.0);

            Assert.IsTrue(mid > low, "Output should increase with speed in gain mode");
            Assert.IsTrue(high > mid, "Output should continue increasing");
        }

        [TestMethod]
        public void Calculate_OutputAlwaysPositive()
        {
            var formula = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.3,
                gain: false);

            for (double x = 0.5; x <= 50.0; x += 1.0)
            {
                double result = formula.Calculate(x);
                Assert.IsTrue(result > 0, $"Output at speed {x} should be positive, got {result}");
            }
        }

        [TestMethod]
        public void Calculate_OutputLessThanOne_DecelStep()
        {
            var formula = new JumpFormula(
                input: 10.0,
                output: 0.5,  // Sensitivity drops to 0.5x
                smooth: 0.0,
                gain: false);

            Assert.AreEqual(1.0, formula.Calculate(5.0), Tolerance);
            Assert.AreEqual(0.5, formula.Calculate(15.0), Tolerance);
        }

        [TestMethod]
        public void Calculate_ZeroSpeed_ReturnsOne()
        {
            var formula = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.3,
                gain: true);

            // In gain mode, x <= 0 should return 1
            Assert.AreEqual(1.0, formula.Calculate(0.0), Tolerance);
        }

        [TestMethod]
        public void Calculate_HigherSmoothValue_WiderTransition()
        {
            var narrowSmooth = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.1,
                gain: false);

            var wideSmooth = new JumpFormula(
                input: 10.0,
                output: 2.0,
                smooth: 0.8,
                gain: false);

            // Far from the step, wider smoothing should show more transition
            double narrowAt5 = narrowSmooth.Calculate(5.0);
            double wideAt5 = wideSmooth.Calculate(5.0);

            // With wider smoothing, should be further from base value at same distance from step
            Assert.IsTrue(wideAt5 > narrowAt5,
                $"Wider smoothing should show more transition: {wideAt5} vs {narrowAt5}");
        }
    }
}
