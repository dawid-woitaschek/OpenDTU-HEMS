# Makefile for OpenDTU-OnBattery tests

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

TEST_OVERSCALING = test_overscaling
TEST_CHARGEDIST  = test_chargedistributor
TEST_WARP = test_warp
TEST_EVSE = test_evse
TEST_MODBUS_CLIENT = test_modbus_client
TEST_PROFILES = test_profiles
TEST_PROFILE_CATALOG = test_profile_catalog
TEST_MODBUS_PROFILES = test_modbus_profiles
TEST_STORAGE = test_storage
TEST_HOST = test_host
TEST_HEMS_MODEL = test_hems_model
TEST_HEMS_CONFIG = test_hems_config
TEST_EV_PROTECTION = test_ev_protection
TEST_FIRMWARE_UPDATE = test_firmware_update
ARDUINOJSON ?= ../.pio/libdeps/generic_esp32s3_usb/ArduinoJson/src

.PHONY: all clean test help

all: $(TEST_OVERSCALING) $(TEST_CHARGEDIST) $(TEST_WARP) $(TEST_EVSE) $(TEST_MODBUS_CLIENT) $(TEST_PROFILES) $(TEST_PROFILE_CATALOG) $(TEST_MODBUS_PROFILES) $(TEST_STORAGE) $(TEST_HOST) $(TEST_HEMS_MODEL) $(TEST_HEMS_CONFIG) $(TEST_EV_PROTECTION) $(TEST_FIRMWARE_UPDATE)

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

$(TEST_PROFILES): test_profiles.cpp ../src/hems/profiles/Profile.cpp ../src/hems/profiles/Store.cpp
	$(CXX) $(CXXFLAGS) -DHEMS_TEST_BUILTIN_PROFILE -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_PROFILE_CATALOG): test_profile_catalog.cpp ../src/hems/profiles/Profile.cpp
	$(CXX) $(CXXFLAGS) -I../include -I$(ARDUINOJSON) -o $@ $^

$(TEST_MODBUS_PROFILES): test_modbus_profiles.cpp ../src/hems/modbus/Protocol.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

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

test: all
	@echo "Running overscaling tests..."
	./$(TEST_OVERSCALING)
	@echo "Running charge current distributor tests..."
	./$(TEST_CHARGEDIST)
	./$(TEST_WARP)
	./$(TEST_EVSE)
	./$(TEST_MODBUS_CLIENT)
	./$(TEST_PROFILES)
	./$(TEST_PROFILE_CATALOG)
	./$(TEST_MODBUS_PROFILES)
	./$(TEST_STORAGE)
	./$(TEST_HOST)
	./$(TEST_HEMS_MODEL)
	./$(TEST_HEMS_CONFIG)
	./$(TEST_EV_PROTECTION)
	./$(TEST_FIRMWARE_UPDATE)

clean:
	rm -f $(TEST_OVERSCALING) $(TEST_CHARGEDIST) $(TEST_WARP) $(TEST_EVSE) $(TEST_MODBUS_CLIENT) $(TEST_PROFILES) $(TEST_PROFILE_CATALOG) $(TEST_MODBUS_PROFILES) $(TEST_STORAGE) $(TEST_HOST) $(TEST_HEMS_MODEL) $(TEST_HEMS_CONFIG) $(TEST_EV_PROTECTION) $(TEST_FIRMWARE_UPDATE)

help:
	@echo "Available targets:"
	@echo "  all    - Build all test executables"
	@echo "  test   - Build and run all tests"
	@echo "  clean  - Remove test executables"
	@echo "  help   - Show this help"
