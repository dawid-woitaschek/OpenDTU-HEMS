// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ESPAsyncWebServer.h>

class WebApiHemsClass {
public:
    void init(AsyncWebServer& server);

private:
    void onStatus(AsyncWebServerRequest* request);
    void onValidationStart(AsyncWebServerRequest* request);
    void onValidationRevoke(AsyncWebServerRequest* request);
    void onValidationCommand(AsyncWebServerRequest* request);
    void onValidationStatus(AsyncWebServerRequest* request);
    void onRecoveryRecord(AsyncWebServerRequest* request);
    void onRecoveryResolve(AsyncWebServerRequest* request);
    void onConfigGet(AsyncWebServerRequest* request);
    void onConfigPost(AsyncWebServerRequest* request);
};
