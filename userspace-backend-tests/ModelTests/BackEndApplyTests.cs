using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using RawAccel.Contracts;
using System;
using System.Collections.Generic;
using System.Linq;
using userspace_backend;
using userspace_backend.Data.Profiles;
using userspace_backend.Data.Profiles.Accel;
using userspace_backend.Driver;
using userspace_backend.IO;
using userspace_backend.Model;
using userspace_backend.Model.AccelDefinitions;
using userspace_backend.Model.AccelDefinitions.Formula;
using DATA = userspace_backend.Data;

namespace userspace_backend_tests.ModelTests
{
    [TestClass]
    public class BackEndApplyTests
    {
        private sealed class StubBackEndLoader : IBackEndLoader
        {
            public IEnumerable<DATA.Device> LoadDevices() => Array.Empty<DATA.Device>();

            public DATA.MappingSet LoadMappings() => new DATA.MappingSet
            {
                Mappings = Array.Empty<DATA.Mapping>(),
                ActiveMappingIndex = 0,
            };

            public IEnumerable<DATA.Profile> LoadProfiles() => Array.Empty<DATA.Profile>();

            public DATA.Settings? LoadSettings() => null;

            public void WriteSettingsToDisk(
                IEnumerable<IDeviceModel> devices,
                MappingsModel mappings,
                IEnumerable<IProfileModel> profiles)
            {
            }

            public void WriteSettings(DATA.Settings settings)
            {
            }
        }

        private sealed class StubSystemDevicesRetriever : ISystemDevicesRetriever
        {
            public IList<ISystemDevice> Devices { get; set; } = new List<ISystemDevice>();

            public IList<ISystemDevice> GetSystemDevices() => Devices;
        }

        private sealed class StubSystemDevice : ISystemDevice
        {
            public string Name { get; init; } = string.Empty;
            public string HWID { get; init; } = string.Empty;
        }

        // Captures whatever the BackEnd hands to its driver. Cross-platform:
        // implements IRawAccelDriver so the same tests run on Windows and Linux
        // builds without touching wrapper.dll or the agent socket.
        private sealed class CapturingDriver : IRawAccelDriver
        {
            public RawAccelConfig? CapturedConfig { get; private set; }
            public int ApplyCount { get; private set; }

            public bool IsAvailable => true;

            public bool Apply(RawAccelConfig config)
            {
                CapturedConfig = config;
                ApplyCount++;
                return true;
            }

            public RawAccelConfig Read() => CapturedConfig ?? new RawAccelConfig();

            public void Deactivate() { }

            public double GetCurrentMouseSpeed() => 0;
        }

        private static (IBackEnd backEnd, CapturingDriver driver) BuildBackEndWithDefaults(
            IList<ISystemDevice>? systemDevices = null)
        {
            var services = new ServiceCollection();
            services.AddSingleton<IBackEndLoader>(new StubBackEndLoader());
            // Register the stub BEFORE Compose; Compose uses TryAddSingleton so our stub wins.
            services.AddSingleton<ISystemDevicesRetriever>(new StubSystemDevicesRetriever
            {
                Devices = systemDevices ?? new List<ISystemDevice>(),
            });
            var driver = new CapturingDriver();
            services.AddSingleton<IRawAccelDriver>(driver);

            var sp = BackEndComposer.Compose(services);
            var backEnd = sp.GetRequiredService<IBackEnd>();
            backEnd.Load();
            return (backEnd, driver);
        }

        private static RawAccelConfig ApplyAndCapture(IBackEnd backEnd, CapturingDriver driver)
        {
            backEnd.Apply();
            Assert.IsNotNull(driver.CapturedConfig, "Apply should have handed a RawAccelConfig to the driver.");
            return driver.CapturedConfig!;
        }

        [TestMethod]
        public void EnsureDefaultMapping_FreshInstall_CreatesMappingWithDefaultEntry()
        {
            var (backEnd, driver) = BuildBackEndWithDefaults();

            Assert.IsTrue(
                backEnd.Mappings.TryGetMapping("Default", out MappingModel? mapping) && mapping != null,
                "A Default mapping should exist after Load on a fresh install.");
            Assert.AreEqual(
                1, mapping!.IndividualMappings.Count,
                "Fresh Default mapping must contain exactly one DefaultDeviceGroup -> Default entry.");
            Assert.AreEqual(DeviceGroups.DefaultDeviceGroup, mapping.IndividualMappings[0].DeviceGroup);
            Assert.AreEqual("Default", mapping.IndividualMappings[0].Profile.Name.ModelValue);

            var cfg = ApplyAndCapture(backEnd, driver);
            Assert.AreEqual(1, cfg.profiles.Count);
            Assert.AreEqual(1, cfg.devices.Count);
        }

        [TestMethod]
        public void EnsureDefaultMapping_StaleEmptyMapping_SelfHealsWithDefaultEntry()
        {
            // Simulate stale mappings.json: {"Mappings":[{"Name":"Default","GroupsToProfiles":{}}],"ActiveMappingIndex":0}
            var staleLoader = new StaleMappingBackEndLoader();
            var services = new ServiceCollection();
            services.AddSingleton<IBackEndLoader>(staleLoader);
            services.AddSingleton<ISystemDevicesRetriever>(new StubSystemDevicesRetriever());
            var driver = new CapturingDriver();
            services.AddSingleton<IRawAccelDriver>(driver);
            var sp = BackEndComposer.Compose(services);
            var backEnd = sp.GetRequiredService<IBackEnd>();
            backEnd.Load();

            Assert.IsTrue(
                backEnd.Mappings.TryGetMapping("Default", out MappingModel? mapping) && mapping != null,
                "Default mapping should still be present after loading stale empty state.");
            Assert.AreEqual(
                1, mapping!.IndividualMappings.Count,
                "Stale empty Default mapping must self-heal to one DefaultDeviceGroup -> Default entry.");

            var cfg = ApplyAndCapture(backEnd, driver);
            Assert.AreEqual(1, cfg.profiles.Count);
            Assert.AreEqual(1, cfg.devices.Count);
        }

        private sealed class StaleMappingBackEndLoader : IBackEndLoader
        {
            public IEnumerable<DATA.Device> LoadDevices() => Array.Empty<DATA.Device>();

            public DATA.MappingSet LoadMappings() => new DATA.MappingSet
            {
                Mappings = new[]
                {
                    new DATA.Mapping
                    {
                        Name = "Default",
                        GroupsToProfiles = new DATA.Mapping.GroupsToProfilesMapping(),
                    },
                },
                ActiveMappingIndex = 0,
            };

            public IEnumerable<DATA.Profile> LoadProfiles() => Array.Empty<DATA.Profile>();
            public DATA.Settings? LoadSettings() => null;
            public void WriteSettingsToDisk(
                IEnumerable<IDeviceModel> devices,
                MappingsModel mappings,
                IEnumerable<IProfileModel> profiles) { }
            public void WriteSettings(DATA.Settings settings) { }
        }

        [TestMethod]
        public void Apply_DefaultState_ProducesOneProfileAndOneDevice()
        {
            var (backEnd, driver) = BuildBackEndWithDefaults();
            var cfg = ApplyAndCapture(backEnd, driver);

            Assert.AreEqual(1, cfg.profiles.Count, "Expected exactly one profile in the DriverConfig.");
            Assert.AreEqual(1, cfg.devices.Count, "Expected exactly one device in the DriverConfig.");

            var device = cfg.devices[0];
            Assert.AreEqual("DEFAULT_DEVICE_ID", device.id);
            Assert.AreEqual(1000, device.config.dpi);
            Assert.AreEqual(1000, device.config.pollingRate);
        }

        [TestMethod]
        public void Apply_DefaultState_DeviceReferencesDefaultProfileByName()
        {
            var (backEnd, driver) = BuildBackEndWithDefaults();
            var cfg = ApplyAndCapture(backEnd, driver);

            var device = cfg.devices[0];
            var profile = cfg.profiles[0];
            Assert.AreEqual(
                profile.name,
                device.profile,
                "Device.profile must match an existing Profile.name so the driver can resolve the mapping.");
        }

        [TestMethod]
        public void Apply_ProfileOutputDpiEdit_FlowsIntoDriverConfig()
        {
            var (backEnd, driver) = BuildBackEndWithDefaults();
            var profile = backEnd.Profiles.Elements[0];

            Assert.IsTrue(profile.OutputDPI.TryUpdateModelDirectly(1600), "OutputDPI update should succeed.");

            var cfg = ApplyAndCapture(backEnd, driver);
            Assert.AreEqual(1600, cfg.profiles[0].outputDPI);
        }

        [TestMethod]
        public void Apply_DeviceDpiEdit_DoesNotAffectPollingRate()
        {
            var (backEnd, driver) = BuildBackEndWithDefaults();
            var device = backEnd.Devices.Elements[0];

            Assert.IsTrue(device.DPI.TryUpdateModelDirectly(3200), "DPI update should succeed.");
            Assert.IsTrue(device.PollRate.TryUpdateModelDirectly(500), "PollRate update should succeed.");

            var cfg = ApplyAndCapture(backEnd, driver);
            Assert.AreEqual(3200, cfg.devices[0].config.dpi);
            Assert.AreEqual(500, cfg.devices[0].config.pollingRate);
        }

        [TestMethod]
        public void ImportSystemDevices_CreatesOneDevicePerSystemDevice()
        {
            var systemDevices = new List<ISystemDevice>
            {
                new StubSystemDevice { Name = "Logitech G Pro",    HWID = @"HID\VID_046D&PID_C54D&MI_00" },
                new StubSystemDevice { Name = "Razer DeathAdder",  HWID = @"HID\VID_1532&PID_0084" },
            };

            var (backEnd, _) = BuildBackEndWithDefaults(systemDevices);
            backEnd.ImportSystemDevices();

            // EnsureDefaultDeviceExists skipped the "Default" placeholder because system devices
            // were present, so the only devices in the list should be the imported ones.
            Assert.AreEqual(2, backEnd.Devices.Elements.Count);

            var imported = backEnd.Devices.Elements
                .Select(d => (d.Name.ModelValue, d.HardwareID.ModelValue))
                .ToList();
            CollectionAssert.Contains(imported, ("Logitech G Pro",   @"HID\VID_046D&PID_C54D&MI_00"));
            CollectionAssert.Contains(imported, ("Razer DeathAdder", @"HID\VID_1532&PID_0084"));

            foreach (var d in backEnd.Devices.Elements)
            {
                Assert.AreEqual(DeviceGroups.DefaultDeviceGroup, d.DeviceGroup.ModelValue,
                    "Imported devices should default to the Default device group.");
            }
        }

        [TestMethod]
        public void ImportSystemDevices_SkipsDevicesAlreadyPresentByHwid()
        {
            var systemDevices = new List<ISystemDevice>
            {
                new StubSystemDevice { Name = "Preloaded Mouse", HWID = @"HID\VID_9999&PID_0001" },
            };

            var (backEnd, _) = BuildBackEndWithDefaults(systemDevices);
            backEnd.ImportSystemDevices();
            Assert.AreEqual(1, backEnd.Devices.Elements.Count);

            // Re-import with the same system device — should be a no-op.
            backEnd.ImportSystemDevices();
            Assert.AreEqual(1, backEnd.Devices.Elements.Count,
                "Repeated ImportSystemDevices calls must not create duplicates when HWID already matches.");
        }

        [TestMethod]
        public void ReloadSystemDevices_RemovesDisconnectedAndAddsNew()
        {
            var initial = new List<ISystemDevice>
            {
                new StubSystemDevice { Name = "Mouse A", HWID = @"HID\VID_AAAA" },
                new StubSystemDevice { Name = "Mouse B", HWID = @"HID\VID_BBBB" },
            };

            var services = new ServiceCollection();
            services.AddSingleton<IBackEndLoader>(new StubBackEndLoader());
            var retrieverStub = new StubSystemDevicesRetriever { Devices = initial };
            services.AddSingleton<ISystemDevicesRetriever>(retrieverStub);
            services.AddSingleton<IRawAccelDriver>(new CapturingDriver());

            var sp = BackEndComposer.Compose(services);
            var backEnd = sp.GetRequiredService<IBackEnd>();
            backEnd.Load();
            backEnd.ImportSystemDevices();
            Assert.AreEqual(2, backEnd.Devices.Elements.Count);

            // Simulate: Mouse A unplugged, Mouse C plugged in.
            retrieverStub.Devices = new List<ISystemDevice>
            {
                new StubSystemDevice { Name = "Mouse B", HWID = @"HID\VID_BBBB" },
                new StubSystemDevice { Name = "Mouse C", HWID = @"HID\VID_CCCC" },
            };

            backEnd.ReloadSystemDevices();

            var hwids = backEnd.Devices.Elements
                .Select(d => d.HardwareID.ModelValue)
                .ToList();
            CollectionAssert.AreEquivalent(
                new[] { @"HID\VID_BBBB", @"HID\VID_CCCC" },
                hwids);
        }

        [TestMethod]
        public void Apply_ProfileCurveCoefficientEdit_FlowsIntoDriverConfig()
        {
            // Regression for the "stale curve on Apply" bug: editing a coefficient inside
            // the currently-selected curve sub-model (Formula -> Classic -> Acceleration)
            // must propagate through EditableSettingsSelector.AnySettingChanged up to
            // ProfileModel.RecalculateDriverData so CurrentValidatedDriverProfile refreshes
            // before BackEnd.Apply() reads it via MapToDriverConfig.
            var (backEnd, driver) = BuildBackEndWithDefaults();
            var profile = backEnd.Profiles.Elements[0];

            Assert.IsTrue(
                profile.Acceleration.DefinitionType.TryUpdateModelDirectly(
                    Acceleration.AccelerationDefinitionType.Formula),
                "Flipping DefinitionType to Formula should succeed.");

            var formulaAccel = (FormulaAccelModel)profile.Acceleration.GetSelectable(
                Acceleration.AccelerationDefinitionType.Formula);

            Assert.IsTrue(
                formulaAccel.FormulaType.TryUpdateModelDirectly(
                    FormulaAccel.AccelerationFormulaType.Classic),
                "Flipping FormulaType to Classic should succeed.");

            var classic = (ClassicAccelerationDefinitionModel)formulaAccel.GetSelectable(
                FormulaAccel.AccelerationFormulaType.Classic);

            const double expectedAcceleration = 0.123;
            Assert.IsTrue(
                classic.Acceleration.TryUpdateModelDirectly(expectedAcceleration),
                "Classic.Acceleration update should succeed.");

            var cfg = ApplyAndCapture(backEnd, driver);
            Assert.AreEqual(AccelMode.classic, cfg.profiles[0].argsX.mode,
                "DriverConfig should reflect the chosen Classic formula.");
            Assert.AreEqual(expectedAcceleration, cfg.profiles[0].argsX.acceleration,
                "DriverConfig should reflect the tweaked Classic.Acceleration coefficient. " +
                "If this fails with the default coefficient, EditableSettingsSelector is not " +
                "propagating nested sub-model changes up to ProfileModel.");
        }

        [TestMethod]
        public void ImportSystemDevices_SyncsInterfaceValueSoUiReflectsRealValues()
        {
            // Regression: EditableSettingV2.TryUpdateModelDirectly used to update ModelValue
            // but not InterfaceValue. The UI binds to InterfaceValue via EditableFieldViewModel,
            // so imported devices showed the DI placeholder ("name", "hwid") even though
            // ModelValue was correct. Guard against that by asserting both properties update.
            var systemDevices = new List<ISystemDevice>
            {
                new StubSystemDevice { Name = "RealMouseName", HWID = @"HID\VID_1234&PID_5678" },
            };

            var (backEnd, _) = BuildBackEndWithDefaults(systemDevices);
            backEnd.ImportSystemDevices();

            var imported = backEnd.Devices.Elements.Single();
            Assert.AreEqual("RealMouseName",                   imported.Name.ModelValue);
            Assert.AreEqual("RealMouseName",                   imported.Name.InterfaceValue,
                "InterfaceValue must mirror ModelValue so the UI shows the imported Name.");
            Assert.AreEqual(@"HID\VID_1234&PID_5678",          imported.HardwareID.ModelValue);
            Assert.AreEqual(@"HID\VID_1234&PID_5678",          imported.HardwareID.InterfaceValue,
                "InterfaceValue must mirror ModelValue so the UI shows the imported HWID.");
        }
    }
}
