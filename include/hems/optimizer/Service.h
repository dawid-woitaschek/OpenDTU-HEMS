// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/optimizer/Planner.h>
#include <hems/optimizer/PlanView.h>
#include <hems/Memory.h>
#include <mutex>
namespace Hems::Optimizer {
class Service {
public:
    void init();
    bool configure(Config const& config);
    bool requestRecalculate();
    void serialize(JsonObject output, bool all, size_t offset = 0) const;
    bool due();
    void run();
    PlanView dispatchPlan() const;
    bool pauseForManual();
    bool resume();
    void invalidate(char const* reason);
private:
    bool persistLocked(Config const& config, bool paused);
    char const* changedLocked(Inputs const& inputs, uint32_t revision) const;
    struct Result {
        Config config;
        Inputs inputs;
        Plan plan;
        uint32_t energyRevision = 0;
    };
    mutable std::mutex _mutex;
    Config _config;
    Load::Profile _load;
    uint32_t _lastObservationMs = 0, _lastHistorySaveMs = 0;
    bool _historyDue = false;
    uint32_t _generation = 0, _lastRunMs = 0, _energyRevision = 0;
    bool _pending = false, _attempted = false;
    bool _manualPaused = false;
    uint32_t _planRevision = 0;
    char const* _replanReason = "startup";
    char const* _error = nullptr;
    Memory::Ptr<Result> _result;
};
extern Service ShadowOptimizer;
} // namespace Hems::Optimizer
