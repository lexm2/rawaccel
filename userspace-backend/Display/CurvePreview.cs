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
        public CurvePreview()
        {
            Points = new ObservableCollection<CurvePoint>();
        }

        public ObservableCollection<CurvePoint> Points { get; }

        public void GeneratePoints(IProfileModel profile)
        {
            // Get the LUT using the same workflow as Apply Settings
            bool gain = profile.Acceleration.FormulaAccel?.Gain.ModelValue ?? false;
            Driver.Types.DriverAccelArgs driverArgs = profile.Acceleration.MapToDriver(gain);

            // Convert based on acceleration mode
            List<CurvePoint> newPoints;

            if (driverArgs.Mode == Driver.Types.AccelMode.NoAccel)
            {
                // NoAccel: flat line at y=1 (no acceleration)
                newPoints = GenerateFlatLine();
            }
            else if (driverArgs.Mode == Driver.Types.AccelMode.Lut && driverArgs.LutData != null)
            {
                // Convert LUT data to CurvePoint objects
                newPoints = ConvertLutToCurvePoints(driverArgs.LutData, driverArgs.LutLength);
            }
            else
            {
                // Unknown mode - generate flat line as fallback
                newPoints = GenerateFlatLine();
            }

            // Replace existing points with new LUT-based points
            SetPoints(newPoints);
        }

        private List<CurvePoint> GenerateFlatLine()
        {
            var points = new List<CurvePoint>();
            // Generate simple flat line from 0 to 100
            for (double speed = 0; speed <= 100; speed += 1.0)
            {
                points.Add(new CurvePoint
                {
                    MouseSpeed = speed,
                    Output = 1.0  // No acceleration = sensitivity multiplier of 1
                });
            }
            return points;
        }

        private List<CurvePoint> ConvertLutToCurvePoints(float[] lutData, int lutLength)
        {
            var points = new List<CurvePoint>();

            // LutData is interleaved (x, y) pairs: [x0, y0, x1, y1, x2, y2, ...]
            // lutLength is the total number of floats (2 * number_of_points)
            for (int i = 0; i < lutLength; i += 2)
            {
                double mouseSpeed = lutData[i];
                double sensitivityMultiplier = lutData[i + 1];

                points.Add(new CurvePoint
                {
                    MouseSpeed = mouseSpeed,
                    Output = sensitivityMultiplier
                });
            }

            return points;
        }

        public void SetPoints(IEnumerable<CurvePoint> points)
        {
            Points.Clear();
            foreach (var point in points)
            {
                Points.Add(point);
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
