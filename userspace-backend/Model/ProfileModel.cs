using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Display;
using userspace_backend.Model.AccelDefinitions;
using userspace_backend.Model.EditableSettings;
using userspace_backend.Model.ProfileComponents;
using DATA = userspace_backend.Data;

namespace userspace_backend.Model
{
    public interface IProfileModel : IEditableSettingsCollectionSpecific<DATA.Profile>
    {
        IEditableSettingSpecific<string> Name { get; }

        IEditableSettingSpecific<int> OutputDPI { get; }

        IEditableSettingSpecific<double> YXRatio { get; }

        IAccelerationModel Acceleration { get; }

        IHiddenModel Hidden { get; }

        ICurvePreview XCurvePreview { get; }

        ICurvePreview YCurvePreview { get; }

        [Obsolete("Use XCurvePreview instead")]
        ICurvePreview CurvePreview { get; }

        string CurrentNameForDisplay { get; }

    }

    public class ProfileModel : NamedEditableSettingsCollection<DATA.Profile>, IProfileModel
    {
        public const string NameDIKey = $"{nameof(ProfileModel)}.{nameof(Name)}";
        public const string OutputDPIDIKey = $"{nameof(ProfileModel)}.{nameof(OutputDPI)}";
        public const string YXRatioDIKey = $"{nameof(ProfileModel)}.{nameof(YXRatio)}";

        public ProfileModel(
            [FromKeyedServices(NameDIKey)]IEditableSettingSpecific<string> name,
            [FromKeyedServices(OutputDPIDIKey)]IEditableSettingSpecific<int> outputDPI,
            [FromKeyedServices(YXRatioDIKey)]IEditableSettingSpecific<double> yxRatio,
            IAccelerationModel acceleration,
            IHiddenModel hidden,
            ICurvePreview xCurvePreview,
            ICurvePreview yCurvePreview
            ) : base(name, [outputDPI, yxRatio], [acceleration, hidden])
        {
            OutputDPI = outputDPI;
            YXRatio = yxRatio;
            Acceleration = acceleration;
            Hidden = hidden;
            XCurvePreview = xCurvePreview;
            YCurvePreview = yCurvePreview;

            // Name and Output DPI do not need to generate a new curve preview
            Name!.PropertyChanged += AnyNonPreviewPropertyChangedEventHandler;
            OutputDPI.PropertyChanged += AnyNonPreviewPropertyChangedEventHandler;

            // The rest of settings should generate a new curve preview
            YXRatio.PropertyChanged += AnyCurvePreviewPropertyChangedEventHandler;
            Acceleration.AnySettingChanged += AnyCurveSettingCollectionChangedEventHandler;
            Hidden.AnySettingChanged += AnyCurveSettingCollectionChangedEventHandler;

            RecalculateCurvePreview();
        }

        public string CurrentNameForDisplay => Name.ModelValue;

        public IEditableSettingSpecific<int> OutputDPI { get; set; }

        public IEditableSettingSpecific<double> YXRatio { get; set; }

        public IAccelerationModel Acceleration { get; set; }

        public IHiddenModel Hidden { get; set; }

        public ICurvePreview XCurvePreview { get; protected set; }

        public ICurvePreview YCurvePreview { get; protected set; }

        [Obsolete("Use XCurvePreview instead")]
        public ICurvePreview CurvePreview => XCurvePreview;

        protected IModelValueValidator<string> NameValidator { get; }

        public override DATA.Profile MapToData()
        {
            return new DATA.Profile()
            {
                Name = Name.ModelValue,
                OutputDPI = OutputDPI.ModelValue,
                YXRatio = YXRatio.ModelValue,
                Acceleration = Acceleration.MapToData(),
                Hidden = Hidden.MapToData(),
            };
        }

        protected void AnyNonPreviewPropertyChangedEventHandler(object? send, PropertyChangedEventArgs e)
        {
            // Name and OutputDPI changes don't affect curve preview
        }

        protected void AnyCurvePreviewPropertyChangedEventHandler(object? send, PropertyChangedEventArgs e)
        {
            if (string.Equals(e.PropertyName, nameof(IEditableSettingSpecific<IComparable>.ModelValue)))
            {
                RecalculateCurvePreview();
            }
        }

        protected void AnyCurveSettingCollectionChangedEventHandler(object? sender, EventArgs e)
        {
            // All settings collections currently require curve preview to be re-generated
            RecalculateCurvePreview();
        }

        protected void RecalculateCurvePreview()
        {
            // Generate X curve points using this profile model
            XCurvePreview.GeneratePoints(this);

            // Generate Y curve points by multiplying X curve outputs by YX ratio
            GenerateYCurvePoints();
        }

        private void GenerateYCurvePoints()
        {
            var yPoints = CreateYCurvePointsFromX(XCurvePreview.Points, YXRatio.ModelValue);
            YCurvePreview.SetPoints(yPoints);
        }

        private List<CurvePoint> CreateYCurvePointsFromX(ObservableCollection<CurvePoint> xPoints, double yxRatio)
        {
            return xPoints.Select(xPoint => new CurvePoint
            {
                MouseSpeed = xPoint.MouseSpeed,
                Output = xPoint.Output * yxRatio
            }).ToList();
        }

        protected override bool TryMapEditableSettingsFromData(DATA.Profile data)
        {
            return Name.TryUpdateModelDirectly(data.Name)
                & OutputDPI.TryUpdateModelDirectly(data.OutputDPI)
                & YXRatio.TryUpdateModelDirectly(data.YXRatio);
        }

        protected override bool TryMapEditableSettingsCollectionsFromData(DATA.Profile data)
        {
            return Acceleration.TryMapFromData(data.Acceleration)
                & Hidden.TryMapFromData(data.Hidden);
        }
    }
}
