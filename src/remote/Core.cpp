// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Core.h>
#include <cctype>
#include <cstdlib>
namespace Remote {
bool validEndpoint(char const* url)
{
    if (!url || strlen(url) > 256 || strncmp(url, "https://", 8)) { return false; }
    auto host = url + 8;
    auto end = strchr(host, '/');
    if (!end || end == host) { return false; } // Explicit path; no ambiguous URL parsing.
    auto colon = static_cast<char const*>(memchr(host, ':', end - host));
    auto hostEnd = colon ? colon : end;
    if (hostEnd - host > 253 || !memchr(host, '.', hostEnd - host)) { return false; }
    size_t label = 0; bool alphabetic = false;
    for (auto p = host; p < hostEnd; ++p) {
        unsigned char c = *p;
        if (c == '.') {
            if (!label || label > 63 || p[-1] == '-') { return false; }
            label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) { return false; }
            if (!label && c == '-') { return false; }
            ++label; alphabetic = alphabetic || std::isalpha(c);
        }
    }
    if (!label || label > 63 || hostEnd[-1] == '-' || !alphabetic) { return false; }
    if (colon) {
        unsigned port = 0;
        if (end - colon < 2 || end - colon > 6) { return false; }
        for (auto p = colon + 1; p < end; ++p) {
            if (*p < '0' || *p > '9') { return false; }
            port = port * 10 + *p - '0';
        }
        if (!port || port > 65535) { return false; }
    }
    // No userinfo, query, fragment, backslash, encoded delimiters or control bytes.
    // A collector endpoint needs only a plain, conservatively encoded path.
    for (auto p = end; *p; ++p) {
        auto c = static_cast<unsigned char>(*p);
        if (!(std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')) { return false; }
    }
    return true;
}
bool validToken(char const* token) {
    if (!token || strlen(token) > 128) { return false; }
    for (; *token; ++token) {
        auto c = static_cast<unsigned char>(*token);
        if (!(std::isalnum(c) || strchr("-._~+/=", c))) { return false; }
    }
    return true;
}
void settingsJson(Settings const& s, JsonObject out, bool persist) {
    out["enabled"] = s.enabled; out["endpoint"] = static_cast<char const*>(s.endpoint);
    out["interval_minutes"] = s.intervalMinutes;
    if (persist) { out["token"] = static_cast<char const*>(s.token); }
    else { out["token_set"] = s.token[0] != 0; }
}
bool parseSettings(JsonVariantConst in, Settings& s, bool persisted) {
    if (!in.is<JsonObjectConst>() || !in["enabled"].is<bool>() || !in["endpoint"].is<char const*>()
        || !in["interval_minutes"].is<uint16_t>()) { return false; }
    Settings candidate = s;
    candidate.enabled = in["enabled"].as<bool>(); candidate.intervalMinutes = in["interval_minutes"].as<uint16_t>();
    auto endpoint = in["endpoint"].as<char const*>();
    if (strlen(endpoint) >= sizeof(candidate.endpoint) || (*endpoint && !validEndpoint(endpoint))
        || !candidate.intervalMinutes || candidate.intervalMinutes > 1440) { return false; }
    strcpy(candidate.endpoint, endpoint);
    if (persisted || !in["token"].isNull()) {
        if (!in["token"].is<char const*>() || !validToken(in["token"].as<char const*>())) { return false; }
        strcpy(candidate.token, in["token"].as<char const*>()); // Empty explicitly clears; omission preserves.
    }
    if (candidate.enabled && (!candidate.endpoint[0] || !candidate.token[0])) { return false; }
    s = candidate; return true;
}
} // namespace Remote
