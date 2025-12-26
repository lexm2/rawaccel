using userspace_backend.Model.EditableSettings;

namespace userspace_backend_tests
{
    /// <summary>
    /// Static helper class providing pre-instantiated parsers for tests.
    /// </summary>
    public static class UserInputParsers
    {
        public static IntParser IntParser { get; } = new IntParser();
        public static DoubleParser DoubleParser { get; } = new DoubleParser();
        public static StringParser StringParser { get; } = new StringParser();
        public static BoolParser BoolParser { get; } = new BoolParser();
    }

    /// <summary>
    /// Static helper class providing pre-instantiated validators for tests.
    /// </summary>
    public static class ModelValueValidators
    {
        public static DefaultModelValueValidator<int> DefaultIntValidator { get; } = new DefaultModelValueValidator<int>();
        public static DefaultModelValueValidator<double> DefaultDoubleValidator { get; } = new DefaultModelValueValidator<double>();
        public static DefaultModelValueValidator<string> DefaultStringValidator { get; } = new DefaultModelValueValidator<string>();
        public static DefaultModelValueValidator<bool> DefaultBoolValidator { get; } = new DefaultModelValueValidator<bool>();
    }
}
