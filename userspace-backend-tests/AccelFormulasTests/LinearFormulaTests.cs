using Microsoft.VisualStudio.TestTools.UnitTesting;
using userspace_backend.Common.AccelFormulas;

namespace userspace_backend_tests.AccelFormulasTests
{
    [TestClass]
    public class LinearFormulaTests
    {
        private const double Tolerance = 0.0001;

        [TestMethod]
        public void Calculate_MatchesClassicWithExponentTwo()
        {
            var linearFormula = new LinearFormula(
                acceleration: 0.05,
                inputOffset: 2.0,
                capY: 3.0,
                gain: false);

            var classicFormula = new ClassicFormula(
                acceleration: 0.05,
                exponent: 2.0,
                inputOffset: 2.0,
                capY: 3.0,
                gain: false);

            // Test at various speeds
            double[] testSpeeds = { 0.5, 1.0, 2.0, 5.0, 10.0, 25.0, 50.0 };

            foreach (double speed in testSpeeds)
            {
                double linearResult = linearFormula.Calculate(speed);
                double classicResult = classicFormula.Calculate(speed);

                Assert.AreEqual(classicResult, linearResult, Tolerance,
                    $"Linear and Classic (exp=2) should match at speed {speed}");
            }
        }

        [TestMethod]
        public void Calculate_BelowOffset_ReturnsOne()
        {
            var formula = new LinearFormula(
                acceleration: 0.1,
                inputOffset: 5.0,
                capY: 3.0,
                gain: false);

            Assert.AreEqual(1.0, formula.Calculate(1.0), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(4.9), Tolerance);
            Assert.AreEqual(1.0, formula.Calculate(5.0), Tolerance); // At offset
        }

        [TestMethod]
        public void Calculate_AboveOffset_IncreasesSensitivity()
        {
            var formula = new LinearFormula(
                acceleration: 0.05,
                inputOffset: 5.0,
                capY: 10.0,
                gain: false);

            double atOffset = formula.Calculate(5.0);
            double aboveOffset = formula.Calculate(10.0);

            Assert.AreEqual(1.0, atOffset, Tolerance);
            Assert.IsTrue(aboveOffset > 1.0, "Sensitivity should increase above offset");
        }

        [TestMethod]
        public void Calculate_GainMode_WorksCorrectly()
        {
            var gainFormula = new LinearFormula(
                acceleration: 0.05,
                inputOffset: 2.0,
                capY: 3.0,
                gain: true);

            var legacyFormula = new LinearFormula(
                acceleration: 0.05,
                inputOffset: 2.0,
                capY: 3.0,
                gain: false);

            // Both should return 1 below offset
            Assert.AreEqual(1.0, gainFormula.Calculate(1.0), Tolerance);

            // Above offset, gain mode should also produce acceleration
            double gainResult = gainFormula.Calculate(10.0);
            Assert.IsTrue(gainResult > 1.0, "Gain mode should produce acceleration above offset");
        }

        [TestMethod]
        public void Calculate_RespectsOutputCap()
        {
            var formula = new LinearFormula(
                acceleration: 0.5,  // High acceleration
                inputOffset: 0.0,
                capY: 2.0,
                gain: false);

            double highSpeedResult = formula.Calculate(100.0);

            // Should not exceed cap
            Assert.IsTrue(highSpeedResult <= 2.0 + Tolerance,
                $"Output {highSpeedResult} should not exceed cap of 2.0");
        }

        [TestMethod]
        public void Calculate_ZeroAcceleration_ReturnsOne()
        {
            var formula = new LinearFormula(
                acceleration: 0.0,
                inputOffset: 0.0,
                capY: 0.0,
                gain: false);

            Assert.AreEqual(1.0, formula.Calculate(50.0), Tolerance);
        }
    }
}
