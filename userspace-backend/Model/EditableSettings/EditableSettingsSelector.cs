using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.Generic;
using System.Linq;
using userspace_backend.Logging;

namespace userspace_backend.Model.EditableSettings
{
    public interface IEditableSettingsSelector<T, U> : IEditableSettingsCollectionSpecific<U> where T : Enum
    {
        public IEditableSettingSpecific<T> Selection { get; }

        public IEditableSettingsCollectionSpecific<U> GetSelectable(T choice);
    }

    public static class EditableSettingsSelectorHelper
    {
        public static string GetSelectionKey<T>(T value) where T : Enum
        {
            return $"{typeof(T)}.{value.ToString()}";
        }

    }

    public abstract class EditableSettingsSelectableIntermediate<T> :
        EditableSettingsCollectionV2<T>
    {
        protected EditableSettingsSelectableIntermediate(
            IEnumerable<IEditableSetting> editableSettings,
            IEnumerable<IEditableSettingsCollectionV2> editableSettingsCollections)
            : base(editableSettings, editableSettingsCollections)
        {
        }
    }

    public abstract class EditableSettingsSelectable<T, U> :
        EditableSettingsSelectableIntermediate<T>,
        IEditableSettingsCollectionSpecific<U> where T : class, U
    {
        protected EditableSettingsSelectable(
            IEnumerable<IEditableSetting> editableSettings,
            IEnumerable<IEditableSettingsCollectionV2> editableSettingsCollections)
            : base(editableSettings, editableSettingsCollections)
        {
        }

        public bool TryMapFromData(U data)
        {
            T dataCasted = data as T;
            return dataCasted == null ? false : TryMapFromData(dataCasted);
        }

        U IEditableSettingsCollectionSpecific<U>.MapToData()
        {
            return MapToData();
        }
    }

    public abstract class EditableSettingsSelector<T, U>
        : EditableSettingsCollectionV2<U>,
          IEditableSettingsSelector<T, U> where T : Enum
    {
        private readonly ILoggingService loggingService;

        protected EditableSettingsSelector(
            IServiceProvider serviceProvider,
            IEditableSettingSpecific<T> selection,
            IEnumerable<IEditableSetting> editableSettings,
            IEnumerable<IEditableSettingsCollectionV2> editableSettingsCollections)
            : base(editableSettings.Union([selection]), editableSettingsCollections)
        {
            loggingService = serviceProvider.GetService<ILoggingService>();
            SelectionLookup = new Dictionary<T, IEditableSettingsCollectionSpecific<U>>();
            InitSelectionLookup(serviceProvider);
            Selection = selection;

            // Subscribe to AnySettingChanged events for all SelectionLookup items
            // This ensures that changes within selected items (e.g., FormulaAccelModel) propagate up
            foreach (var selectableItem in SelectionLookup.Values)
            {
                selectableItem.AnySettingChanged += EditableSettingsCollectionChangedEventHandler;
                loggingService?.LogDebug(LogSource.Backend,
                    "EditableSettingsSelector<{SelectorType}>: Subscribed to {ItemType}.AnySettingChanged",
                    typeof(T).Name, selectableItem.GetType().Name);
            }
        }

        public IEditableSettingSpecific<T> Selection { get; }

        protected IDictionary<T, IEditableSettingsCollectionSpecific<U>> SelectionLookup { get; }

        public IEditableSettingsCollectionSpecific<U> GetSelectable(T choice) => SelectionLookup[choice];

        public IEditableSettingsCollectionSpecific<U> Selected => GetSelectable(Selection.ModelValue);

        protected void InitSelectionLookup(IServiceProvider serviceProvider)
        {
            foreach (T value in Enum.GetValues(typeof(T)))
            {
                string key = EditableSettingsSelectorHelper.GetSelectionKey(value);
                SelectionLookup.Add(value, serviceProvider.GetRequiredKeyedService<IEditableSettingsCollectionSpecific<U>>(key));
            }
        }

        public override U MapToData()
        {
            return Selected.MapToData();
        }
    }

    public abstract class EditableSettingsSelectableSelector<T, U, V>
        : EditableSettingsSelector<T, U> ,
        IEditableSettingsCollectionSpecific<V> where U : class, V where T : Enum
    {
        protected EditableSettingsSelectableSelector(
            IServiceProvider serviceProvider,
            IEditableSettingSpecific<T> selection,
            IEnumerable<IEditableSetting> editableSettings,
            IEnumerable<IEditableSettingsCollectionV2> editableSettingsCollections)
            : base(serviceProvider, selection, editableSettings, editableSettingsCollections)
        {
        }

        public bool TryMapFromData(V data)
        {
            U dataCasted = data as U;
            return dataCasted == null ? false : TryMapFromData(dataCasted);
        }

        V IEditableSettingsCollectionSpecific<V>.MapToData()
        {
            return MapToData();
        }
    }
}
