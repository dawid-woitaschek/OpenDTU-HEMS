// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <Configuration.h>
#include <hems/actuation/Cleanup.h>
#include <hems/actuation/CommandAuthorization.h>
#include <hems/actuation/Mailbox.h>
#include <hems/evse/Actuator.h>
#include <hems/evse/Settings.h>
#include <hems/modbus/Client.h>
#include <hems/profiles/Profile.h>
#include <hems/warp/State.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <mutex>

namespace Hems::Warp {

// Boot lifetime, like the owning HemsController; safe for late DNS completion.
class Provider {
public:
    void configure(WarpConfig const& config, Evse::BindingConfig const& binding, bool monitor);
    void loop(); // Scheduler only: dispatches, never performs network I/O.
    Snapshot snapshot() const;
    bool controlCapable() const;

    void setAuthorization(void* context, Actuation::AuthorizationCallback callback);
    bool submitCommand(Actuation::Command const& command, uint32_t& id);
    bool cancelPendingCommand(uint32_t id = 0);
    Actuation::MailboxSnapshot commandSnapshot() const;
    Actuation::CleanupSnapshot cleanupSnapshot() const;

private:
    struct CommandIoContext {
        Provider* provider = nullptr;
        uint32_t generation = 0;
    };

    struct CleanupIoContext {
        Provider* provider = nullptr;
        uint32_t token = 0;
    };

    struct CleanupContext {
        bool valid = false;
        WarpConfig config {};
        std::shared_ptr<Profiles::Profile const> profile;
        uint8_t unit = 1;
        uint32_t token = 0;
    };

    static void workerEntry(void* context);
    void worker();
    bool ensureWorkerLocked();
    bool commandAuthorized(uint32_t generation) const;
    bool cleanupAuthorized(uint32_t token) const;
    void retainCleanupLocked(WarpConfig const& config,
            std::shared_ptr<Profiles::Profile const> const& profile, uint8_t unit);
    void processCommand(Actuation::Envelope const& envelope);
    void processCleanup();
    static bool commandIoAuthorized(void* context);
    static Modbus::Result commandIoWrite(void* context, uint16_t address,
            uint16_t count, uint16_t const* values);
    static Modbus::Result commandIoRead(void* context, uint16_t address,
            uint16_t count, uint8_t* payload);
    static bool cleanupIoAuthorized(void* context);
    static Modbus::Result cleanupIoWrite(void* context, uint16_t address,
            uint16_t count, uint16_t const* values);
    static Modbus::Result cleanupIoRead(void* context, uint16_t address,
            uint16_t count, uint8_t* payload);

    mutable std::mutex _mutex;
    WarpConfig _config {};
    std::shared_ptr<Profiles::Profile const> _profile;
    bool _profileRequested = false;
    bool _enabled = false;
    bool _busy = false;
    bool _polled = false;
    bool _controlOwned = false;
    uint32_t _generation = 0;
    uint32_t _lastPollMs = 0;
    uint8_t _unit = 1;
    State _state;
    Modbus::Client _client;
    TaskHandle_t _worker = nullptr;

    Actuation::Mailbox _mailbox;
    Evse::Actuator _actuator;
    uint32_t _actuatorGeneration = 0;
    CleanupContext _cleanup;
    Actuation::CleanupState _cleanupState = Actuation::CleanupState::Idle;
    uint32_t _cleanupAttempts = 0;
    uint32_t _cleanupLastAttemptMs = 0;
    uint32_t _nextCleanupToken = 1;
    Actuation::CommandResult _cleanupLastResult {};
    void* _authorizationContext = nullptr;
    Actuation::AuthorizationCallback _authorization = nullptr;
};

} // namespace Hems::Warp
