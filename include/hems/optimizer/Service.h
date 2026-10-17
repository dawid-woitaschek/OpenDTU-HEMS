// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/optimizer/Planner.h>
#include <memory>
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
private:
    struct Result {
        Config config;
        Inputs inputs;
        Plan plan;
        uint32_t energyRevision = 0;
    };
    mutable std::mutex _mutex;
    Config _config;
    uint32_t _generation = 0, _lastRunMs = 0, _energyRevision = 0;
    bool _pending = false, _attempted = false;
    char const* _error = nullptr;
    std::unique_ptr<Result> _result;
};
extern Service ShadowOptimizer;
} // namespace Hems::Optimizer
