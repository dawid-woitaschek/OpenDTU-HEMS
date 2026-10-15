// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/recovery/Journal.h>
#include <array>
#include <cstring>
#include <vector>

class MemoryJournalFiles : public Hems::Recovery::Files {
public:
    std::array<std::vector<uint8_t>, 2> committed, pending;
    std::array<bool, 2> hasCommitted {}, hasPending {};
    bool fail = false, ioError = false, partial = false;
    unsigned commits = 0;
    Hems::Recovery::LoadResult read(Hems::ControlTarget target, bool temp, uint8_t* data,
            size_t capacity, size_t& size) override {
        using Hems::Recovery::LoadResult;
        if (ioError) { return LoadResult::Error; }
        size_t i = static_cast<size_t>(target);
        if (!(temp ? hasPending[i] : hasCommitted[i])) { size = 0; return LoadResult::Missing; }
        auto const& bytes = temp ? pending[i] : committed[i];
        size = bytes.size();
        if (size > capacity) { return LoadResult::Error; }
        std::memcpy(data, bytes.data(), size);
        return LoadResult::Present;
    }
    bool replace(Hems::ControlTarget target, uint8_t const* data, size_t size) override {
        ++commits;
        size_t i = static_cast<size_t>(target);
        if (fail && !partial) { return false; }
        hasPending[i] = true;
        pending[i].assign(data, data + (partial ? size / 2 : size));
        if (fail || partial) { return false; }
        committed[i] = pending[i]; hasCommitted[i] = true;
        hasPending[i] = false; pending[i].clear();
        return true;
    }
};
