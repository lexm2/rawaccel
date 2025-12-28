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
        private string interfaceValue;

        /// <summary>
        /// This value can be bound in UI for logic based on validated input
        /// </summary>
        [ObservableProperty]
        private T modelValue;

        public T CurrentValidatedValue => ModelValue;

        public EditableSetting(
            string settingLabel,
            T initialValue,
            IUserInputParser<T> parser,
            IModelValueValidator<T> validator,
            bool autoUpdateFromInterface = false,
            string localizationKey = null)
        {
            SettingLabel = settingLabel;
            LocalizationKey = localizationKey;
            LastWrittenValue = initialValue;
            Parser = parser;
            Validator = validator;
            UpdateModelValueFromLastKnown();
            SetInterfaceToModel();
            AutoUpdateFromInterface = autoUpdateFromInterface;
        }

        /// <summary>
        /// Label text for this setting in UI
        /// </summary>
        public string SettingLabel { get; }

        public string LocalizationKey { get; set; }

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

        public bool HasChanged() => ModelValue.CompareTo(LastWrittenValue) != 0;

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

        partial void OnModelValueChanged(T value)
        {
            // This partial method hook ensures that the CommunityToolkit.Mvvm source generator
            // properly wires up PropertyChanged notifications when ModelValue changes.
            // The method body can remain empty - its presence is what matters for the generator.
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
            SetInterfaceToModel();  // Sync InterfaceValue with updated ModelValue
            return true;
        }
    }
}
