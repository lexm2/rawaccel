namespace userinterface.Services.Events;

/// <summary>
/// Published when the application language changes.
/// </summary>
public record LanguageChangedEvent(string NewLanguage);

/// <summary>
/// Published when the application theme changes.
/// </summary>
public record ThemeChangedEvent(string NewTheme);
