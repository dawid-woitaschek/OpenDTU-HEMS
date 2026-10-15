// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/actuation/CommandAuthorization.h>
#include <hems/actuation/Mailbox.h>
#include <hems/modbus/Client.h>
#include <hems/storage/Actuator.h>
#include <hems/storage/Settings.h>
#include <hems/storage/State.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <mutex>

namespace Hems::Storage {

class Provider {
public:
    void configure(StorageConfig const& config, bool monitor);
    void loop(); // Scheduler only: dispatches, never performs network I/O.
    Snapshot snapshot() const;
    bool controlCapable() const;

    void setAuthorization(void* context, Actuation::AuthorizationCallback callback);
    bool submitCommand(Actuation::Command const& command, uint32_t& id);
    bool cancelPendingCommand(uint32_t id = 0);
    Actuation::MailboxSnapshot commandSnapshot() const;

private:
    struct CommandIoContext {
        Provider* provider = nullptr;
        uint32_t generation = 0;
    };

    static void workerEntry(void* context);
    void worker();
    bool ensureWorkerLocked();
    bool commandAuthorized(uint32_t generation) const;
    void processCommand(Actuation::Envelope const& envelope);
    static bool commandIoAuthorized(void* context);
    static Modbus::Result commandIoWrite(void* context, uint16_t address,
            uint16_t count, uint16_t const* values);
    static Modbus::Result commandIoRead(void* context, uint16_t address,
            uint16_t count, uint8_t* payload);

    mutable std::mutex _mutex;
    StorageConfig _config {};
    std::shared_ptr<Profiles::Profile const> _profile;
    bool _enabled = false;
    bool _busy = false;
    bool _polled = false;
    uint32_t _generation = 0;
    uint32_t _lastPollMs = 0;
    uint16_t _port = 0;
    uint8_t _unit = 0;
    State _state;
    Modbus::Client _client;
    TaskHandle_t _worker = nullptr;

    Actuation::Mailbox _mailbox;
    Actuator _actuator;
    uint32_t _actuatorGeneration = 0;
    void* _authorizationContext = nullptr;
    Actuation::AuthorizationCallback _authorization = nullptr;
};

} // namespace Hems::Storage
