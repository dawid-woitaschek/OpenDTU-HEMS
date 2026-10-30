#pragma once
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) (void)(mux)
#define portEXIT_CRITICAL(mux) (void)(mux)
#define portENTER_CRITICAL_SAFE(mux) (void)(mux)
#define portEXIT_CRITICAL_SAFE(mux) (void)(mux)
