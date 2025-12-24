using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using userspace_backend.Display.Calculations;
using userspace_backend.Driver;
using userspace_backend.Model;

namespace userspace_backend.Display
{
    public interface ICurvePreview
    {
        ObservableCollection<CurvePoint> Points { get; }

        void GeneratePoints(IProfileModel profile);

        void SetPoints(IEnumerable<CurvePoint> points);
    }

    public class CurvePreview : ICurvePreview
    {
        private readonly IAccelerationCalculatorFactory _calculatorFactory;

        public CurvePreview(IAccelerationCalculatorFactory calculatorFactory)
        {
            _calculatorFactory = calculatorFactory;
            Points = new ObservableCollection<CurvePoint>();
            InitPoints();
        }

        public ObservableCollection<CurvePoint> Points { get; }

        public void GeneratePoints(IProfileModel profile)
        {
            IAccelerationCalculator accel = _calculatorFactory.Create(profile).CreateStatelessCopy();

            foreach (CurvePoint point in Points)
            {
                var output = accel.Accelerate(point.MouseSpeed, 0, 1, 1);
                var outputSpeed = Math.Sqrt(Math.Pow(output.x, 2) + Math.Pow(output.y, 2));
                point.Output = outputSpeed / point.MouseSpeed;
            }
        }

        public void SetPoints(IEnumerable<CurvePoint> points)
        {
            Points.Clear();
            foreach (var point in points)
            {
                Points.Add(point);
            }
        }

        protected void InitPoints()
        {
            ICollection<double> speeds = CurveCalculationHelpers.CalculateCurvePointSpeeds();
            
            foreach (double speed in speeds)
            {
                Points.Add(new CurvePoint() { MouseSpeed = speed, Output = 0.0 });
            }
        }
    }

    public partial class CurvePoint : ObservableObject
    {
        [ObservableProperty]
        public double mouseSpeed;

        [ObservableProperty]
        public double output;
    }

}
