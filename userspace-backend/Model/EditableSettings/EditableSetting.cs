using CommunityToolkit.Mvvm.ComponentModel;
using System;

namespace userspace_backend.Model.EditableSettings
{
    public partial class EditableSetting<T> : ObservableObject, IEditableSettingSpecific<T> where T : IComparable
    {
        /// <summary>
        /// This value can be bound in UI for direct editing
        /// </summary>
        [ObservableProperty]
        public string interfaceValue;

        /// <summary>
        /// This value can be bound in UI for logic based on validated input
        /// </summary>
        [ObservableProperty]
        public T modelValue;

        public T CurrentValidatedValue => ModelValue;

        public EditableSetting(
            string displayName,
            T initialValue,
            IUserInputParser<T> parser,
            IModelValueValidator<T> validator,
            bool autoUpdateFromInterface = false,
            string localizationKey = null)
        {
            DisplayName = displayName;
            LocalizationKey = localizationKey;
            LastWrittenValue = initialValue;
            Parser = parser;
            Validator = validator;
            UpdateModelValueFromLastKnown();
            UpdateInterfaceValue();
            AutoUpdateFromInterface = autoUpdateFromInterface;
        }

        /// <summary>
        /// Display name for this setting in UI
        /// </summary>
        /// TODO: Make private and only use DisplayText for UI
        public string DisplayName { get; }

        /// <summary>
        /// Optional localization key for this setting. If provided, DisplayText will use localized string instead of DisplayName
        /// </summary>
        public string LocalizationKey { get; set; }

        /// <summary>
        /// Gets the display text for this setting. Returns LocalizationKey if provided, otherwise DisplayName.
        /// UI layer should handle actual localization of the key.
        /// </summary>
        public string DisplayText =>
            !string.IsNullOrEmpty(LocalizationKey)
                ? LocalizationKey
                : DisplayName ?? string.Empty;

        public string EditedValueForDiplay => InterfaceValue;

        public T LastWrittenValue { get; protected set; }

        /// <summary>
        /// Interface can set this for cases when new value arrives all at once (such as menu selection)
        /// instead of cases where new value arrives in parts (typing)
        /// </summary>
        public bool AutoUpdateFromInterface { get; set; }

        private IUserInputParser<T> Parser { get; }

        //TODO: change settings collections init so that this can be made private for non-static validators
        public IModelValueValidator<T> Validator { get; set; }

        public bool HasChanged() => ModelValue.CompareTo(LastWrittenValue) == 0;

        public bool TryUpdateFromInterface()
        {
            if (string.IsNullOrEmpty(InterfaceValue))
            {
                UpdateInterfaceValue();
                return false;
            }

            if (!Parser.TryParse(InterfaceValue.Trim(), out T parsedValue))
            {
                UpdateInterfaceValue();
                return false;
            }

            if (parsedValue.CompareTo(ModelValue) == 0)
            {
                return true;
            }

            if (Validator == null)
            {
                throw new InvalidOperationException(
                    $"Validator is null for EditableSetting '{DisplayName}'. " +
                    $"InterfaceValue: '{InterfaceValue}', " +
                    $"ParsedValue: '{parsedValue}', " +
                    $"ModelValue: '{ModelValue}', " +
                    $"Parser: {Parser?.GetType().Name ?? "null"}");
            }

            if (!Validator.Validate(parsedValue))
            {
                UpdateInterfaceValue();
                return false;
            }

            UpdatedModeValue(parsedValue);
            return true;
        }

        protected void UpdateInterfaceValue()
        {
            InterfaceValue = ModelValue?.ToString();
        }

        protected void UpdateModelValueFromLastKnown()
        {
            UpdatedModeValue(LastWrittenValue);
        }

        protected void UpdatedModeValue(T value)
        {
            ModelValue = value;
        }

        partial void OnInterfaceValueChanged(string value)
        {
            if (AutoUpdateFromInterface)
            {
                TryUpdateFromInterface();
            }
        }

        public bool TryUpdateModelDirectly(T data)
        {
            if (data.CompareTo(ModelValue) == 0)
            {
                return true;
            }

            if (!Validator.Validate(data))
            {
                UpdateInterfaceValue();
                return false;
            }

            UpdatedModeValue(data);
            UpdateInterfaceValue();
            return true;
        }
    }

    public partial class EditableSettingV2<T> : ObservableObject, IEditableSettingSpecific<T> where T : IComparable
    {
        /// <summary>
        /// This value can be bound in UI for direct editing
        /// </summary>
        [ObservableProperty]
        public string interfaceValue;

        /// <summary>
        /// This value can be bound in UI for logic based on validated input
        /// </summary>
        [ObservableProperty]
        public T modelValue;

        public T CurrentValidatedValue => ModelValue;

        public EditableSettingV2(
            string displayName,
            T initialValue,
            IUserInputParser<T> parser,
            IModelValueValidator<T> validator,
            bool autoUpdateFromInterface = false,
            string localizationKey = null)
        {
            DisplayName = displayName;
            LocalizationKey = localizationKey;
            LastWrittenValue = initialValue;
            Parser = parser;
            Validator = validator;
            UpdateModelValueFromLastKnown();
            SetInterfaceToModel();
            AutoUpdateFromInterface = autoUpdateFromInterface;
        }

        /// <summary>
        /// Display name for this setting in UI
        /// </summary>
        public string DisplayName { get; }

        public string LocalizationKey { get; set; }

        public string DisplayText =>
            !string.IsNullOrEmpty(LocalizationKey)
                ? LocalizationKey
                : DisplayName ?? string.Empty;

        public string EditedValueForDiplay => InterfaceValue;

        public T LastWrittenValue { get; protected set; }

        /// <summary>
        /// Interface can set this for cases when new value arrives all at once (such as menu selection)
        /// instead of cases where new value arrives in parts (typing)
        /// </summary>
        public bool AutoUpdateFromInterface { get; set; }

        private IUserInputParser<T> Parser { get; }

        //TODO: change settings collections init so that this can be made private for non-static validators
        public IModelValueValidator<T> Validator { get; set; }

        private bool AllowAutoUpdateFromInterface { get; set; } = true;

        public bool HasChanged() => ModelValue.CompareTo(LastWrittenValue) == 0;

        public bool TryUpdateFromInterface()
        {
            bool result = TryUpdateFromInterfaceImpl(out bool editedInterfaceNeedsReset);

            if (editedInterfaceNeedsReset)
            {
                SetInterfaceToModel();
            }

            return result;
        }

        protected bool TryUpdateFromInterfaceImpl(out bool editedInterfaceNeedsReset)
        {
            editedInterfaceNeedsReset = true;

            if (string.IsNullOrEmpty(InterfaceValue))
            {
                return false;
            }

            if (!Parser.TryParse(InterfaceValue.Trim(), out T parsedValue))
            {
                return false;
            }

            return TryUpdateModelDirectlyImpl(parsedValue, out editedInterfaceNeedsReset);
        }

        protected void SetInterfaceToModel()
        {
            bool previous = AllowAutoUpdateFromInterface;
            AllowAutoUpdateFromInterface = false;
            InterfaceValue = ModelValue?.ToString();
            AllowAutoUpdateFromInterface = true;
        }

        protected void UpdateModelValueFromLastKnown()
        {
            UpdateModeValue(LastWrittenValue);
        }

        protected void UpdateModeValue(T value)
        {
            ModelValue = value;
        }

        partial void OnInterfaceValueChanged(string value)
        {
            // TODO: double-check race conditions
            if (AutoUpdateFromInterface && AllowAutoUpdateFromInterface)
            {
                TryUpdateFromInterface();
            }
        }

        // TODO: unit test
        public bool TryUpdateModelDirectly(T data)
        {
            return TryUpdateModelDirectlyImpl(data, out _);
        }

        private bool TryUpdateModelDirectlyImpl(T data, out bool editedInterfaceNeedsReset)
        {
            editedInterfaceNeedsReset = false;

            if (data.CompareTo(ModelValue) == 0)
            {
                return true;
            }

            if (!Validator.Validate(data))
            {
                editedInterfaceNeedsReset = true;
                return false;
            }

            UpdateModeValue(data);
            return true;
        }
    }
}
