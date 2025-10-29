using System.ComponentModel;
using System.Globalization;

namespace userinterface.Services;

public interface ILocalizationService : INotifyPropertyChanged
{
    bool TryChangeLanguage(string cultureCode, out CultureInfo? culture);

    void ChangeLanguage(string cultureCode);

    string GetText(string key);
}
