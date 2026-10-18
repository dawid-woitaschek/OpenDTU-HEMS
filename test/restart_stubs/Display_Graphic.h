#pragma once
struct DisplayStub { bool enabled = true; void setStatus(bool status) { enabled = status; } };
extern DisplayStub Display;
