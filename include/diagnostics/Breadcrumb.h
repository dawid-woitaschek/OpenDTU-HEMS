// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
namespace Diagnostics {
enum class Component : uint8_t { None, Energy, Optimizer, FirmwareUpdate, Configuration, Network, Maintenance, Runtime, RemoteDiagnostics, Count };
enum class Operation : uint8_t { None, Tariff, Forecast, Calculate, Upload, Write, Reset, Restore, Apply, Reconnect, Boot, Restart, Count };
enum class Phase : uint8_t { Start, ProviderStart, Dns, Connect, TlsHttp, Decode, Aggregate, Configuration, Network, Complete, Count };
enum class RestartSource : uint8_t { None, MaintenanceReboot, FirmwareUpdate, ConfigurationReset, FileRestore, PinMappingChange, InverterUnresponsive, Unspecified, Count };
constexpr size_t ActivitySlots = 6;
struct Activity {
    uint32_t sequence, uptimeMs;
    Component component;
    Operation operation;
    Phase phase;
    int8_t subIndex;
    uint8_t active, reserved[3];
};
// POD only: RTC_NOINIT storage must never have a C++ dynamic initializer.
struct Record {
    uint32_t magic;
    uint16_t version, size;
    uint32_t generation, sequence, restartRequestedAt, restartAt;
    RestartSource restartSource;
    uint8_t restartRequested, intentionalRestart, espRestartCalled;
    uint8_t shutdownRegistered, reserved[3];
    Activity activities[ActivitySlots];
    uint32_t checksum;
};
static_assert(sizeof(Record) == 132, "Breadcrumb must stay small and fixed");
struct Storage { Record slots[2]; };
bool valid(Record const& record);
Activity const* selected(Record const& record);
char const* name(Component value);
char const* name(Operation value);
char const* name(Phase value);
char const* name(RestartSource value);
class Journal {
public:
    void boot(Storage& storage, bool retain);
    uint32_t begin(Component component, Operation operation, Phase phase, int8_t index, uint32_t ms);
    void phase(uint32_t token, Phase phase, int8_t index, uint32_t ms);
    void finish(uint32_t token, uint32_t ms);
    void restartRequested(RestartSource source, uint32_t ms);
    void intentionalRestart(uint32_t ms);
    void espRestart();
    void hookRegistered(bool registered);
    Record current {}, previous {};
    bool previousValid = false;
private:
    void commit();
    uint32_t next();
    Storage* _storage = nullptr;
    unsigned _slot = 0;
};
} // namespace Diagnostics
