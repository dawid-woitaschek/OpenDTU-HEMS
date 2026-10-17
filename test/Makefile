# Makefile for OpenDTU-OnBattery tests

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

TEST_OVERSCALING = test_overscaling
TEST_CHARGEDIST  = test_chargedistributor
TEST_WARP = test_warp
TEST_EVSE = test_evse
TEST_MODBUS_CLIENT = test_modbus_client
TEST_MODBUS_WRITE_CLIENT = test_modbus_write_client
TEST_PROFILES = test_profiles
TEST_PROFILE_CONTROL = test_profile_control
TEST_PROFILE_CATALOG = test_profile_catalog
TEST_MODBUS_PROFILES = test_modbus_profiles
TEST_CONTROL_EXECUTION = test_control_execution
TEST_SEMANTIC_ACTUATORS = test_semantic_actuators
TEST_PROVIDER_CLEANUP = test_provider_cleanup
TEST_CONTROL_READINESS = test_control_readiness
TEST_ACTUATION_MAILBOX = test_actuation_mailbox
TEST_STORAGE = test_storage
TEST_HOST = test_host
TEST_HEMS_MODEL = test_hems_model
TEST_HEMS_CONFIG = test_hems_config
TEST_EV_PROTECTION = test_ev_protection
TEST_OPTIMIZER_SERVICE = test_optimizer_service
TEST_TRANSPORT = test_energy_transport
TEST_NATIVE_ENERGY = test_native_energy
TEST_SHADOW = test_shadow_optimizer
TEST_ENERGY = test_energy
TEST_ENERGY_SERVICE = test_energy_service
TEST_SETTINGS_STORES = test_settings_stores
TEST_FIRMWARE_UPDATE = test_firmware_update
ARDUINOJSON ?= ../.pio/libdeps/generic_esp32s3_usb/ArduinoJson/src

.PHONY: all clean test help

all: $(TEST_OVERSCALING) $(TEST_CHARGEDIST) $(TEST_WARP) $(TEST_EVSE) $(TEST_MODBUS_CLIENT) $(TEST_MODBUS_WRITE_CLIENT) $(TEST_PROFILES) $(TEST_PROFILE_CONTROL) $(TEST_PROFILE_CATALOG) $(TEST_MODBUS_PROFILES) $(TEST_CONTROL_EXECUTION) $(TEST_SEMANTIC_ACTUATORS) $(TEST_PROVIDER_CLEANUP) $(TEST_CONTROL_READINESS) $(TEST_ACTUATION_MAILBOX) $(TEST_STORAGE) $(TEST_HOST) $(TEST_HEMS_MODEL) $(TEST_HEMS_CONFIG) $(TEST_EV_PROTECTION) $(TEST_FIRMWARE_UPDATE) $(TEST_OPTIMIZER_SERVICE) $(TEST_TRANSPORT) $(TEST_NATIVE_ENERGY) $(TEST_SHADOW) $(TEST_ENERGY) $(TEST_ENERGY_SERVICE) $(TEST_SETTINGS_STORES)

$(TEST_OVERSCALING): test_overscaling.cpp ../src/OverscalingCalculator.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $< ../src/OverscalingCalculator.cpp

$(TEST_CHARGEDIST): test_chargedistributor.cpp ../src/solarcharger/ChargeCurrentDistributor.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

$(TEST_WARP): test_warp.cpp ../src/hems/modbus/Protocol.cpp ../src/hems/warp/State.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

$(TEST_EVSE): test_evse.cpp ../src/hems/evse/Telemetry.cpp ../src/hems/warp/ProfileAdapter.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_MODBUS_CLIENT): test_modbus_client.cpp ../src/hems/modbus/Client.cpp ../src/hems/modbus/Protocol.cpp
	$(CXX) $(CXXFLAGS) -Iwarp_stubs -I../include -pthread -o $@ $^

$(TEST_MODBUS_WRITE_CLIENT): test_modbus_write_client.cpp ../src/hems/modbus/Client.cpp ../src/hems/modbus/Protocol.cpp
	$(CXX) $(CXXFLAGS) -Iwarp_stubs -I../include -pthread -o $@ $^

$(TEST_PROFILES): test_profiles.cpp ../src/hems/profiles/Profile.cpp ../src/hems/profiles/Store.cpp
	$(CXX) $(CXXFLAGS) -DHEMS_TEST_BUILTIN_PROFILE -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_PROFILE_CONTROL): test_profile_control.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_PROFILE_CATALOG): test_profile_catalog.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_MODBUS_PROFILES): test_modbus_profiles.cpp ../src/hems/modbus/Protocol.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

$(TEST_CONTROL_EXECUTION): test_control_execution.cpp ../src/hems/profiles/ControlPlan.cpp ../src/hems/actuation/Executor.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_SEMANTIC_ACTUATORS): ../src/hems/profiles/Profile.cpp test_semantic_actuators.cpp ../src/hems/profiles/ControlPlan.cpp ../src/hems/actuation/Executor.cpp ../src/hems/actuation/ProfileSession.cpp ../src/hems/storage/Actuator.cpp ../src/hems/evse/Actuator.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_PROVIDER_CLEANUP): ../src/hems/Controller.cpp ../src/hems/actuation/Readiness.cpp ../src/hems/policies/EvProtection.cpp ../src/hems/model/Normalize.cpp test_provider_cleanup.cpp ../src/hems/storage/Provider.cpp ../src/hems/warp/Provider.cpp ../src/hems/storage/State.cpp ../src/hems/warp/State.cpp ../src/hems/storage/Telemetry.cpp ../src/hems/evse/Telemetry.cpp ../src/hems/warp/ProfileAdapter.cpp ../src/hems/profiles/Profile.cpp ../src/hems/profiles/ControlPlan.cpp ../src/hems/actuation/Executor.cpp ../src/hems/actuation/ProfileSession.cpp ../src/hems/actuation/Mailbox.cpp ../src/hems/actuation/Capabilities.cpp ../src/hems/storage/Actuator.cpp ../src/hems/evse/Actuator.cpp ../src/hems/modbus/Protocol.cpp
	$(CXX) $(CXXFLAGS) -Iprovider_stubs -Iwarp_stubs -I../include -I$(ARDUINOJSON) -pthread -o $@ $^

$(TEST_CONTROL_READINESS): test_control_readiness.cpp ../src/hems/actuation/Readiness.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

$(TEST_ACTUATION_MAILBOX): test_actuation_mailbox.cpp ../src/hems/actuation/Mailbox.cpp
	$(CXX) $(CXXFLAGS) -I../include -pthread -o $@ $^

$(TEST_STORAGE): test_storage.cpp ../src/hems/storage/Telemetry.cpp ../src/hems/storage/State.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_HOST): test_host.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $<

$(TEST_HEMS_MODEL): test_hems_model.cpp ../src/hems/model/Normalize.cpp ../src/hems/constraints/Constraints.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_HEMS_CONFIG): test_hems_config.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_EV_PROTECTION): test_ev_protection.cpp ../src/hems/policies/EvProtection.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

$(TEST_FIRMWARE_UPDATE): test_firmware_update.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $<

$(TEST_OPTIMIZER_SERVICE): test_optimizer_service.cpp ../src/hems/optimizer/Service.cpp ../src/hems/optimizer/Planner.cpp ../src/hems/energy/Service.cpp ../src/hems/energy/Native.cpp ../src/hems/energy/Provider.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -Ioptimizer_stubs -Ienergy_stubs -Iprovider_stubs -I../include -I$(ARDUINOJSON) -pthread -o $@ $^

$(TEST_TRANSPORT): test_energy_transport.cpp ../src/hems/energy/Transport.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -Itransport_stubs -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_NATIVE_ENERGY): test_native_energy.cpp ../src/hems/energy/Native.cpp ../src/hems/energy/Provider.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_SHADOW): test_shadow_optimizer.cpp ../src/hems/optimizer/Planner.cpp ../src/hems/energy/Provider.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_ENERGY): test_energy.cpp ../src/hems/energy/Provider.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_ENERGY_SERVICE): test_energy_service.cpp ../src/hems/energy/Native.cpp ../src/hems/energy/Service.cpp ../src/hems/energy/Provider.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -Ienergy_stubs -Iprovider_stubs -I../include -I$(ARDUINOJSON) -pthread -o $@ $^

$(TEST_SETTINGS_STORES): test_settings_stores.cpp ../src/hems/evse/SettingsStore.cpp ../src/hems/storage/SettingsStore.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -Ienergy_stubs -Iprovider_stubs -I../include -I$(ARDUINOJSON) -pthread -o $@ $^

test: all
	python3 test_control_surface.py
	@echo "Running overscaling tests..."
	./$(TEST_OVERSCALING)
	@echo "Running charge current distributor tests..."
	./$(TEST_CHARGEDIST)
	./$(TEST_WARP)
	./$(TEST_EVSE)
	./$(TEST_MODBUS_CLIENT)
	./$(TEST_MODBUS_WRITE_CLIENT)
	./$(TEST_PROFILES)
	./$(TEST_PROFILE_CONTROL)
	./$(TEST_PROFILE_CATALOG)
	./$(TEST_MODBUS_PROFILES)
	./$(TEST_CONTROL_EXECUTION)
	./$(TEST_SEMANTIC_ACTUATORS)
	./$(TEST_PROVIDER_CLEANUP)
	./$(TEST_CONTROL_READINESS)
	./$(TEST_ACTUATION_MAILBOX)
	./$(TEST_STORAGE)
	./$(TEST_HOST)
	./$(TEST_HEMS_MODEL)
	./$(TEST_HEMS_CONFIG)
	./$(TEST_EV_PROTECTION)
	./$(TEST_FIRMWARE_UPDATE)
	./$(TEST_OPTIMIZER_SERVICE)
	./$(TEST_TRANSPORT)
	./$(TEST_NATIVE_ENERGY)
	./$(TEST_SHADOW)
	./$(TEST_ENERGY)
	./$(TEST_ENERGY_SERVICE)
	./$(TEST_SETTINGS_STORES)

clean:
	rm -f $(TEST_OVERSCALING) $(TEST_CHARGEDIST) $(TEST_WARP) $(TEST_EVSE) $(TEST_MODBUS_CLIENT) $(TEST_MODBUS_WRITE_CLIENT) $(TEST_PROFILES) $(TEST_PROFILE_CONTROL) $(TEST_PROFILE_CATALOG) $(TEST_MODBUS_PROFILES) $(TEST_CONTROL_EXECUTION) $(TEST_SEMANTIC_ACTUATORS) $(TEST_PROVIDER_CLEANUP) $(TEST_CONTROL_READINESS) $(TEST_ACTUATION_MAILBOX) $(TEST_STORAGE) $(TEST_HOST) $(TEST_HEMS_MODEL) $(TEST_HEMS_CONFIG) $(TEST_EV_PROTECTION) $(TEST_FIRMWARE_UPDATE) $(TEST_OPTIMIZER_SERVICE) $(TEST_TRANSPORT) $(TEST_NATIVE_ENERGY) $(TEST_SHADOW) $(TEST_ENERGY) $(TEST_ENERGY_SERVICE) $(TEST_SETTINGS_STORES)

help:
	@echo "Available targets:"
	@echo "  all    - Build all test executables"
	@echo "  test   - Build and run all tests"
	@echo "  clean  - Remove test executables"
	@echo "  help   - Show this help"
