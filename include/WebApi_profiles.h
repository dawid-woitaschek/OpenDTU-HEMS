// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ESPAsyncWebServer.h>
class WebApiProfilesClass {
public:
    void init(AsyncWebServer& server);
private:
    void list(AsyncWebServerRequest* request);
    void exportProfile(AsyncWebServerRequest* request);
    void erase(AsyncWebServerRequest* request);
    void inspect(AsyncWebServerRequest* request);
    void finish(AsyncWebServerRequest* request, bool persist);
    void body(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
};
