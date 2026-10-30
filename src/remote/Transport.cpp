// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Service.h>
#include <remote/Allocator.h>
#include <RuntimeDiagnostics.h>
#include <hems/energy/Trust.h>
#include <algorithm>
#include <cctype>
#include <strings.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <memory>
#include <new>
namespace Remote {
namespace {
class Client final : public WiFiClientSecure {
public:
    int dns = -1, tlsError = 0;
    int connect(char const* host, uint16_t port, int32_t timeout) override {
        _timeout = timeout;
        IPAddress address;
        Diagnostics::workerPhase(Diagnostics::Phase::Dns);
        dns = WiFi.hostByName(host, address) ? 1 : 0;
        if (!dns) { return 0; }
        Diagnostics::workerPhase(Diagnostics::Phase::Connect);
        Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::BeforeConnect);
        // The hostname overload preserves certificate hostname validation and SNI.
        auto connected = WiFiClientSecure::connect(address, port, host, Hems::Energy::ProviderRoots, nullptr, nullptr);
        char ignored[1]; tlsError = lastError(ignored, sizeof(ignored));
        Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Connected);
        if (connected) { Diagnostics::workerPhase(Diagnostics::Phase::TlsHttp); }
        return connected;
    }
};
// Small bounded HTTP/1.0 exchange. No header/body Strings and no library HTTP
// response-header logging (a collector could echo a credential in a header).
struct Reader {
    Client& client;
    uint32_t started, lastByte;
    bool closed = false;
    int read() {
        while (millis() - started < 30000 && millis() - lastByte < 5000) {
            if (client.available() > 0) {
                int c = client.read();
                if (c >= 0) { lastByte = millis(); return c; }
            } else if (!client.connected()) { closed = true; return -1; }
            delay(1);
        }
        return -1;
    }
    bool line(char* out, size_t size, size_t& remaining) {
        size_t used = 0;
        while (remaining) {
            int c = read(); --remaining;
            if (c < 0) { return false; }
            if (c == '\r') {
                if (!remaining) { return false; }
                --remaining;
                if (read() != '\n') { return false; }
                out[used] = 0; return true;
            }
            if (c == '\n' || c == 0 || (c < 32 && c != '\t') || used + 1 == size) { return false; }
            out[used++] = c;
        }
        return false;
    }
};
bool writeAll(Client& client, char const* data, size_t length, uint32_t started) {
    while (length && millis() - started < 30000) {
        auto written = client.write(reinterpret_cast<uint8_t const*>(data), std::min(length, size_t(1024)));
        if (!written) { return false; }
        data += written; length -= written;
    }
    return !length && millis() - started < 30000;
}
bool request(Settings const& settings, char const* body, size_t length, Transmission const& flight, int& result, char const*& error) {
    if (!validEndpoint(settings.endpoint) || !validToken(settings.token) || !settings.token[0] || !body || !length || length > MaxPayload) {
        error = "invalid_request"; return false;
    }
    auto client = std::unique_ptr<Client>(new (std::nothrow) Client());
    if (!client) { error = "insufficient_memory"; return false; }
    client->setCACert(Hems::Energy::ProviderRoots); client->setHandshakeTimeout(10); client->setTimeout(5);
    auto path = strchr(settings.endpoint + 8, '/');
    auto colon = static_cast<char const*>(memchr(settings.endpoint + 8, ':', path - settings.endpoint - 8));
    char host[254]; size_t hostLength = (colon ? colon : path) - settings.endpoint - 8;
    memcpy(host, settings.endpoint + 8, hostLength); host[hostLength] = 0;
    uint16_t port = colon ? static_cast<uint16_t>(strtoul(colon + 1, nullptr, 10)) : 443;
    auto started = millis();
    if (!client->connect(host, port, 5000)) {
        error = client->dns == 0 ? "dns_failed" : client->tlsError < -1 ? "tls_failed" : "connect_or_timeout"; return false;
    }
    char header[640];
    auto size = snprintf(header, sizeof(header), "POST %s HTTP/1.0\r\nHost: %.*s\r\nUser-Agent: OpenDTU-HEMS\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\nAuthorization: Bearer ",
        path, int(path - settings.endpoint - 8), settings.endpoint + 8, unsigned(length));
    if (size <= 0 || size >= int(sizeof(header)) || !writeAll(*client, header, size, started)
        || !writeAll(*client, settings.token, strlen(settings.token), started)
        || !writeAll(*client, "\r\n\r\n", 4, started) || !writeAll(*client, body, length, started)) {
        error = "write_timeout_or_connection_lost"; return false;
    }
    Reader reader {*client, started, millis()};
    size_t remainingHeaders = 4096;
    char line[256];
    if (!reader.line(line, sizeof(line), remainingHeaders) || strlen(line) < 12
        || (strncmp(line, "HTTP/1.0 ", 9) && strncmp(line, "HTTP/1.1 ", 9))
        || !std::isdigit(line[9]) || !std::isdigit(line[10]) || !std::isdigit(line[11])
        || (line[12] && line[12] != ' ')) { error = "invalid_http_response"; return false; }
    result = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    int expected = -1;
    constexpr size_t MaxAck = 1024;
    for (;;) {
        if (!reader.line(line, sizeof(line), remainingHeaders)) { error = "http_header_limit_or_timeout"; return false; }
        if (!line[0]) { break; }
        auto value = strchr(line, ':');
        if (!value) { error = "invalid_http_response"; return false; }
        *value++ = 0;
        while (*value == ' ' || *value == '\t') { ++value; }
        if (!strcasecmp(line, "Transfer-Encoding") || !strcasecmp(line, "Content-Encoding")) {
            // Chunked/compressed acknowledgements are unnecessary for this small
            // HTTP/1.0 response and must never bypass the byte limit.
            error = "unsupported_response_encoding"; return false;
        }
        if (!strcasecmp(line, "Content-Length")) {
            if (expected >= 0 || !*value) { error = "invalid_http_response"; return false; }
            expected = 0;
            for (; *value; ++value) {
                if (*value < '0' || *value > '9' || expected > int(MaxAck)) { error = "invalid_acknowledgement"; return false; }
                expected = expected * 10 + *value - '0';
            }
            if (expected > int(MaxAck)) { error = "invalid_acknowledgement"; return false; }
        }
    }
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Response);
    if (result < 200 || result >= 300) { error = "http_error"; return false; }
    char response[MaxAck + 1]; size_t used = 0;
    while (used < MaxAck && (expected < 0 || used < size_t(expected))) {
        int c = reader.read(); if (c < 0) { break; } response[used++] = c;
    }
    bool complete = expected >= 0 ? used == size_t(expected) : used < MaxAck && reader.closed;
    client->stop();
    if (!complete || !used) { error = "acknowledgement_timeout_or_size"; return false; }
    response[used] = 0;
    Allocator allocator(8192); JsonDocument ack(&allocator);
    // A bounded reader lets us reject trailing JSON/data as well as malformed JSON.
    struct JsonReader {
        char const* data; size_t length, position = 0;
        int read() { return position < length ? static_cast<unsigned char>(data[position++]) : -1; }
        size_t readBytes(char* out, size_t count) {
            size_t n = 0; while (n < count && position < length) { out[n++] = data[position++]; } return n;
        }
    } jsonReader {response, used};
    auto decoded = deserializeJson(ack, jsonReader, DeserializationOption::NestingLimit(3));
    bool trailing = false; int c;
    while ((c = jsonReader.read()) >= 0) { if (c != ' ' && c != '\t' && c != '\r' && c != '\n') { trailing = true; } }
    if (decoded || ack.overflowed() || trailing || !flight.accepts(ack.as<JsonVariantConst>())) {
        error = "invalid_acknowledgement"; return false;
    }
    error = nullptr; return true;
}
}
bool post(Settings const& settings, char const* body, size_t length, Transmission const& flight, int& result, char const*& error) {
    try { return request(settings, body, length, flight, result, error); }
    catch (std::bad_alloc const&) { error = "insufficient_memory"; return false; }
}
} // namespace Remote
