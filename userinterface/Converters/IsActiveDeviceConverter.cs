using Avalonia.Data.Converters;
using Microsoft.Extensions.DependencyInjection;
using System;
using System.Globalization;
using userspace_backend;

namespace userinterface.Converters
{
    public class IsActiveDeviceConverter : IValueConverter
    {
        public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            // Hardware detection has been removed - no device is considered "active"
            // This converter can be removed or extended with different logic in the future
            return false;
        }

        public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}