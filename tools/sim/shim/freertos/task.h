/* Host shim: implemented in stubs.c (the harness has no scheduler). */
#pragma once
#include "freertos/FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
