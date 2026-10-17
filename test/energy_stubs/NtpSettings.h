// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

struct NtpSettingsClass {
    bool synced = true;
    bool isTimeSynced() const { return synced; }
};

inline NtpSettingsClass NtpSettings;