using Microsoft.VisualStudio.TestTools.UnitTesting;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend_tests.AccelFormulasTests
{
    [TestClass]
    public class NaturalFormulaTests
    {
        private const double Tolerance = 0.0001;

        [TestMethod]
        public void Calculate_BelowOffset_ReturnsOne()
        {
            var formula = new NaturalFormula(
                decayRate: 0.5,
                inputOffset: 5.0,
                limit: 3.0,
                gain: false);

            Assert.AreEqual(1.0, formula.Calculate(1.0), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(4.9), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(5.0), Tolerance); // At offset
        }

        [TestMethod]
        public void Calculate_AboveOffset_ApproachesLimit()
        {
            double limit = 3.0;
            var formula = new NaturalFormula(
                decayRate: 0.5,
                inputOffset: 0.0,
                limit: limit,
                gain: false);

            double lowSpeed = formula.Calculate(1.0);
            double mediumSpeed = formula.Calculate(10.0);
            double highSpeed = formula.Calculate(100.0);
            double veryHighSpeed = formula.Calculate(500.0);

            // Should increase monotonically
            Assert.IsTrue(mediumSpeed > lowSpeed, "Should increase with speed");
            Assert.IsTrue(highSpeed > mediumSpeed, "Should continue increasing");

            // At very high speeds, should approach but not exceed limit
            Assert.IsTrue(veryHighSpeed <= limit + Tolerance,
                $"Output {veryHighSpeed} should approach limit {limit}");
            Assert.IsTrue(veryHighSpeed > limit - 0.5,
                $"Output {veryHighSpeed} should be close to limit {limit} at high speed");
        }

        [TestMethod]
        public void Calculate_LegacyMode_AsymptoticBehavior()
        {
            var formula = new NaturalFormula(
                decayRate: 1.0,
                inputOffset: 0.0,
                limit: 2.0,
                gain: false);

            // The decay should be exponential, approaching the limit asymptotically
            double result1 = formula.Calculate(10.0);
            double result2 = formula.Calculate(20.0);
            double result3 = formula.Calculate(40.0);

            // Each step should get closer to the limit but with diminishing returns
            double diff1 = 2.0 - result1;
            double diff2 = 2.0 - result2;
            double diff3 = 2.0 - result3;

            Assert.IsTrue(diff2 < diff1, "Should get closer to limit");
            Assert.IsTrue(diff3 < diff2, "Should continue getting closer");
        }

        [TestMethod]
        public void Calculate_GainMode_DifferentBehavior()
        {
            var legacyFormula = new NaturalFormula(
                decayRate: 0.5,
                inputOffset: 2.0,
                limit: 3.0,
                gain: false);

            var gainFormula = new NaturalFormula(
                decayRate: 0.5,
                inputOffset: 2.0,
                limit: 3.0,
                gain: true);

            // Both should be 1 below offset
            Assert.AreEqual(1.0, legacyFormula.Calculate(1.0), Tolerance);
            Assert.AreEqual(1.0, gainFormula.Calculate(1.0), Tolerance);

            // Above offset, both should accelerate
            Assert.IsTrue(legacyFormula.Calculate(10.0) > 1.0);
            Assert.IsTrue(gainFormula.Calculate(10.0) > 1.0);
        }

        [TestMethod]
        public void Calculate_HigherDecayRate_FasterApproach()
        {
            var slowDecay = new NaturalFormula(
                decayRate: 0.2,
                inputOffset: 0.0,
                limit: 3.0,
                gain: false);

            var fastDecay = new NaturalFormula(
                decayRate: 1.0,
                inputOffset: 0.0,
                limit: 3.0,
                gain: false);

            // At same speed, higher decay rate should be closer to limit
            double slowResult = slowDecay.Calculate(10.0);
            double fastResult = fastDecay.Calculate(10.0);

            Assert.IsTrue(fastResult > slowResult,
                $"Higher decay rate should approach limit faster: {fastResult} vs {slowResult}");
        }

        [TestMethod]
        public void Calculate_OutputAlwaysPositive()
        {
            var formula = new NaturalFormula(
                decayRate: 0.5,
                inputOffset: 2.0,
                limit: 3.0,
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
            var formula = new NaturalFormula(
                decayRate: 0.5,
                inputOffset: 1.0,
                limit: 3.0,
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
    }
}
