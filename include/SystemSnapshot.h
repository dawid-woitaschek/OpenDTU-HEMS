// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
// Read only: never creates an observer checkpoint. Callers own their JSON allocator.
void systemSnapshot(JsonObject root, bool heapHistory = false);
void currentHeapSnapshot(JsonObject out);
