using Avalonia.Data.Converters;
using System;
using System.Globalization;
using userspace_backend.Model;

namespace userinterface.Converters
{
    public class DeviceNameConverter : IValueConverter
    {
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            if (value is ISystemDevice device)
            {
                return string.IsNullOrWhiteSpace(device.Name) ? device.HWID : device.Name;
            }
            return value?.ToString() ?? string.Empty;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}