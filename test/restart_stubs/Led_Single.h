#pragma once
struct LedStub { bool off = false; void turnAllOff() { off = true; } };
extern LedStub LedSingle;
