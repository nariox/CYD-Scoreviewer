/* Host shim: monotonic microsecond clock (implemented in stubs.c). */
#pragma once
#include <stdint.h>

int64_t sim_timer_get_time(void);
#define esp_timer_get_time() sim_timer_get_time()
