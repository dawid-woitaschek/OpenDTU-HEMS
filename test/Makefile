# Makefile for OpenDTU-OnBattery tests

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

TEST_OVERSCALING = test_overscaling
TEST_CHARGEDIST  = test_chargedistributor
TEST_WARP = test_warp
TEST_MODBUS_CLIENT = test_modbus_client

.PHONY: all clean test help

all: $(TEST_OVERSCALING) $(TEST_CHARGEDIST) $(TEST_WARP) $(TEST_MODBUS_CLIENT)

$(TEST_OVERSCALING): test_overscaling.cpp ../src/OverscalingCalculator.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $< ../src/OverscalingCalculator.cpp

$(TEST_CHARGEDIST): test_chargedistributor.cpp ../src/solarcharger/ChargeCurrentDistributor.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

$(TEST_WARP): test_warp.cpp ../src/hems/modbus/Protocol.cpp ../src/hems/warp/State.cpp
	$(CXX) $(CXXFLAGS) -I../include -o $@ $^

$(TEST_MODBUS_CLIENT): test_modbus_client.cpp ../src/hems/modbus/Client.cpp ../src/hems/modbus/Protocol.cpp
	$(CXX) $(CXXFLAGS) -Iwarp_stubs -I../include -pthread -o $@ $^

test: all
	@echo "Running overscaling tests..."
	./$(TEST_OVERSCALING)
	@echo "Running charge current distributor tests..."
	./$(TEST_CHARGEDIST)
	./$(TEST_WARP)
	./$(TEST_MODBUS_CLIENT)

clean:
	rm -f $(TEST_OVERSCALING) $(TEST_CHARGEDIST) $(TEST_WARP) $(TEST_MODBUS_CLIENT)

help:
	@echo "Available targets:"
	@echo "  all    - Build all test executables"
	@echo "  test   - Build and run all tests"
	@echo "  clean  - Remove test executables"
	@echo "  help   - Show this help"
