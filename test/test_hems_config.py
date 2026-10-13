#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production HEMS JSON helpers with the existing ArduinoJson dependency.

Usage: python test/test_hems_config.py .pio/libdeps/generic_esp32s3_usb/ArduinoJson/src
Only the surrounding ConfigurationClass declaration/platform bindings are shimmed.
No alternate serializer or deserializer is implemented by the test.
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
arduino_json = pathlib.Path(sys.argv[1]).resolve()
source = (root / "src/Configuration.cpp").read_text()
start = source.index("void ConfigurationClass::serializeHemsConfig(")
end = source.index("void ConfigurationClass::serializeHttpRequestConfig(", start)
bindings = """
#include <ArduinoJson.h>
#include <hems/warp/Settings.h>
#include <Arduino.h>
#include <cassert>
#include <cstring>
#include <string>
#define CONFIG_VERSION_HEMS 1
struct HemsConfig { uint32_t Version; bool Enabled; WarpConfig Warp; };
class ConfigurationClass {
public:
    static void serializeHemsConfig(HemsConfig const&, JsonObject&);
    static void deserializeHemsConfig(JsonObject const&, HemsConfig&);
    static bool deserializeWarpConfig(JsonVariantConst, WarpConfig&);
};
"""
tests = r"""
int main() {
    HemsConfig config {};
    JsonDocument document;
    auto load = [&](char const* text) {
        assert(!deserializeJson(document, text));
        JsonObject obj = document.as<JsonObject>();
        ConfigurationClass::deserializeHemsConfig(obj, config);
    };
    auto warpAccepted = [&](std::string const& text, WarpConfig& candidate) {
        assert(!deserializeJson(document, text.c_str()));
        return ConfigurationClass::deserializeWarpConfig(document.as<JsonVariantConst>(), candidate);
    };

    load("{}");
    assert(config.Version == 1 && !config.Enabled && !config.Warp.Enabled && config.Warp.Port == 502);
    load(R"({"version":1,"enabled":true})");
    assert(config.Version == 1 && config.Enabled && !config.Warp.Enabled);
    load(R"({"version":99,"enabled":true})");
    assert(config.Version != CONFIG_VERSION_HEMS); // Controller rejects unsupported version.
    load(R"({"version":"1","enabled":"true"})");
    assert(config.Version == 0 && !config.Enabled);
    load(R"({"version":1,"enabled":true,"warp":{"enabled":true,"host":"warp.local","port":502}})");
    assert(config.Warp.Enabled && strcmp(config.Warp.Host, "warp.local") == 0 && config.Warp.Port == 502);

    JsonDocument saved;
    JsonObject out = saved.to<JsonObject>();
    ConfigurationClass::serializeHemsConfig(config, out);
    JsonObject backup = saved.as<JsonObject>();
    HemsConfig restored {};
    ConfigurationClass::deserializeHemsConfig(backup, restored);
    assert(restored.Version == 1 && restored.Enabled && restored.Warp.Enabled);
    assert(strcmp(restored.Warp.Host, "warp.local") == 0 && restored.Warp.Port == 502);

    WarpConfig candidate;
    assert(warpAccepted(R"({"enabled":true,"host":"warp.local","port":1})", candidate));
    assert(candidate.Enabled && candidate.Port == 1 && strcmp(candidate.Host, "warp.local") == 0);
    assert(warpAccepted(R"({"enabled":true,"host":"10.0.4.20","port":65535})", candidate));
    assert(candidate.Enabled && candidate.Port == 65535);

    std::string maxHost = std::string(63, 'a') + "." + std::string(63, 'b') + "."
        + std::string(63, 'c') + "." + std::string(61, 'd');
    assert(maxHost.size() == WARP_MAX_HOST_LENGTH);
    assert(warpAccepted(std::string("{\"enabled\":true,\"host\":\"") + maxHost
        + "\",\"port\":502}", candidate));
    assert(strcmp(candidate.Host, maxHost.c_str()) == 0);

    for (auto text : {
            R"({"enabled":true,"host":"","port":502})",
            R"({"enabled":true,"host":"http://warp","port":502})",
            R"({"enabled":true,"host":"131.143.9311.11","port":502})",
            R"({"enabled":true,"host":" warp.local","port":502})",
            R"({"enabled":true,"host":"warp.local ","port":502})",
            R"({"enabled":true,"host":"warp","port":0})",
            R"({"enabled":true,"host":"warp","port":65536})",
            R"({"enabled":true,"host":"warp","port":-1})",
            R"({"enabled":true,"host":"warp","port":502.5})",
            R"({"enabled":true,"host":"warp","port":"502"})",
            R"({"enabled":"true","host":"warp","port":502})",
            R"({"enabled":true,"port":502})",
            R"({"enabled":true,"host":null,"port":502})",
            R"({"enabled":true,"host":"warp"})",
            R"("malformed")",
            R"([])" }) {
        candidate.Enabled = true;
        assert(!warpAccepted(text, candidate));
        assert(!candidate.Enabled); // Malformed restored data cannot activate polling.
    }

    std::string oversizedHost(WARP_MAX_HOST_LENGTH + 1, 'a');
    assert(!warpAccepted(std::string("{\"enabled\":true,\"host\":\"") + oversizedHost
        + "\",\"port\":502}", candidate));

    // Disabled WARP uses a canonical clean payload. The backend deliberately remains
    // strict instead of persisting malformed hidden endpoint data.
    assert(warpAccepted(R"({"enabled":false,"host":"","port":502})", candidate));
    assert(!candidate.Enabled && candidate.Host[0] == '\0' && candidate.Port == 502);
    assert(!warpAccepted(R"({"enabled":false,"host":"bad host","port":502})", candidate));
    assert(!warpAccepted(R"({"enabled":false,"host":"","port":0})", candidate));
    assert(!warpAccepted(R"({"enabled":false,"host":"warp.local","port":"502"})", candidate));

    JsonDocument absent;
    candidate.Enabled = true;
    strcpy(candidate.Host, "old.local");
    candidate.Port = 1234;
    assert(ConfigurationClass::deserializeWarpConfig(absent.as<JsonVariantConst>(), candidate));
    assert(!candidate.Enabled && candidate.Host[0] == '\0' && candidate.Port == 502);

    load(R"({"version":1,"enabled":true,"warp":{"enabled":true,"host":"bad host","port":502}})");
    assert(config.Enabled && !config.Warp.Enabled);
    load(R"({"version":1,"enabled":false,"warp":{"enabled":false,"host":"","port":502}})");
    assert(!config.Enabled && !config.Warp.Enabled && config.Warp.Port == 502);
}
"""
with tempfile.TemporaryDirectory(prefix="hems-config-") as directory:
    harness = pathlib.Path(directory) / "test.cpp"
    binary = pathlib.Path(directory) / "test"
    harness.write_text(bindings + source[start:end] + tests)
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra",
        "-I" + str(arduino_json), "-I" + str(root / "include"),
        "-I" + str(root / "test/warp_stubs"), str(harness), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("Production HEMS JSON compatibility, round-trip and WARP validation tests passed")
