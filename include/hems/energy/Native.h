// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/energy/Provider.h>
#include <hems/energy/Diagnostics.h>
namespace Hems::Energy {
struct NativeInfo {
    int16_t httpStatus = 0;
    int32_t rateLimit = -1, rateRemaining = -1;
    uint32_t retrySeconds = 0;
    int32_t tlsError = 0; // WiFiClientSecure connect result, when negative; -1 is ambiguous.
    int8_t dns = -1; // -1: not attempted, 0: resolution failed, 1: resolved.
    char httpError[40] {}, tlsErrorText[96] {};
    uint32_t durationMs = 0, payloadBytes = 0, jsonPeak = 0;
    MemorySample before, connected, decoded, released;
};
bool parseUtc(char const* text, uint32_t& utc, bool bareUtc = false);
bool parseEnergyCharts(JsonVariantConst json, uint32_t fetchedAt, Series& output, char const*& error);
bool parseForecastSolar(JsonVariantConst json, Series& output, NativeInfo& info, char const*& error);
bool aggregate(Series const& first, Series const& second, Series& output, char const*& error);
// Fixed native origins. HTTPS implementation validates CA chain and DNS identity; no redirects.
bool providerGet(char const* url, Source source, JsonDocument& output, NativeInfo& info, char const*& error);
void forecastUrl(Plane const& plane, char* output, size_t capacity);
void germanyDay(uint32_t utc, unsigned ahead, uint32_t& start, uint32_t& end);
void tariffUrl(uint32_t utc, char* output, size_t capacity);
} // namespace Hems::Energy
