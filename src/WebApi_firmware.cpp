// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2026 Thomas Basler and others
 */
#include "WebApi_firmware.h"
#include "Configuration.h"
#include "FirmwareUpdateValidation.h"
#include "RestartHelper.h"
#include <RuntimeDiagnostics.h>
#include "WebApi.h"
#include "helper.h"
#include "__compiled_constants.h"
#include <AsyncJson.h>
#include <Update.h>
#include "esp_partition.h"
#include "RuntimeData.h"

void WebApiFirmwareClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    using std::placeholders::_4;
    using std::placeholders::_5;
    using std::placeholders::_6;

    server.on("/api/firmware/update", HTTP_POST,
        std::bind(&WebApiFirmwareClass::onFirmwareUpdateFinish, this, _1),
        std::bind(&WebApiFirmwareClass::onFirmwareUpdateUpload, this, _1, _2, _3, _4, _5, _6));

    server.on("/api/firmware/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiFirmwareClass::onFirmwareStatus, this, _1)));
}

bool WebApiFirmwareClass::otaSupported() const
{
    const esp_partition_t* pOtaPartition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    return (pOtaPartition != nullptr);
}

void WebApiFirmwareClass::onFirmwareUpdateFinish(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    auto* firmwareParam = request->getParam("firmware", true, true);
    if (firmwareParam != nullptr
            && FirmwareUpdateValidation::isFactoryImageFilename(firmwareParam->value().c_str())) {
        auto* response = request->beginResponse(400, asyncsrv::T_text_plain,
            "Factory images cannot be installed via OTA. Use a normal firmware .bin image.");
        response->addHeader(asyncsrv::T_Connection, asyncsrv::T_close);
        response->addHeader(asyncsrv::T_CORS_ACAO, "*");
        request->send(response);
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse((Update.hasError()) ? 500 : 200,
        asyncsrv::T_text_plain, (Update.hasError()) ? "FAIL" : "OK");
    response->addHeader(asyncsrv::T_Connection, asyncsrv::T_close);
    response->addHeader(asyncsrv::T_CORS_ACAO, "*");
    request->send(response);

    Diagnostics::finish(_diagnosticToken);
    RuntimeData.write(10);
    RestartHelper.triggerRestart(Diagnostics::RestartSource::FirmwareUpdate);
}

void WebApiFirmwareClass::onFirmwareUpdateUpload(AsyncWebServerRequest* request, String filename,
        size_t index, uint8_t* data, size_t len, bool final)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    if (FirmwareUpdateValidation::isFactoryImageFilename(filename.c_str())) {
        return;
    }

    if (!otaSupported()) {
        return request->send(500, asyncsrv::T_text_plain, "OTA updates not supported");
    }

    if (!index) {
        _diagnosticToken = Diagnostics::begin(Diagnostics::Component::FirmwareUpdate, Diagnostics::Operation::Upload);
        if (!request->hasParam("MD5", true)) {
            Diagnostics::finish(_diagnosticToken);
            return request->send(400, asyncsrv::T_text_plain, "MD5 parameter missing");
        }

        if (!Update.setMD5(request->getParam("MD5", true)->value().c_str())) {
            Diagnostics::finish(_diagnosticToken);
            return request->send(400, asyncsrv::T_text_plain, "MD5 parameter invalid");
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            Update.printError(Serial);
            Diagnostics::finish(_diagnosticToken);
            return request->send(400, asyncsrv::T_text_plain, "OTA could not begin");
        }
    }

    if (len) {
        if (Update.write(data, len) != len) {
            Diagnostics::finish(_diagnosticToken);
            return request->send(400, asyncsrv::T_text_plain, "OTA could not begin");
        }
    }

    if (final) {
        if (!Update.end(true)) {
            Update.printError(Serial);
            Diagnostics::finish(_diagnosticToken);
            return request->send(400, asyncsrv::T_text_plain, "Could not end OTA");
        }
    } else {
        return;
    }
}

void WebApiFirmwareClass::onFirmwareStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();

    root["ota_supported"] = otaSupported();
    root["git_hash"] = __COMPILED_GIT_HASH__;
    root["git_sha"] = __COMPILED_GIT_SHA__;
    root["git_branch"] = __COMPILED_GIT_BRANCH__;
    root["pioenv"] = PIOENV;
    root["update_repository"] = __COMPILED_UPDATE_REPOSITORY__;
    root["update_channel"] = __COMPILED_UPDATE_CHANNEL__;

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
