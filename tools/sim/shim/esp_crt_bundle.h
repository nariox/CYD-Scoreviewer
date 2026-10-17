/* Host shim: TLS bundle attach is a no-op symbol for linking espn_api.c. */
#pragma once
#include "esp_err.h"

esp_err_t esp_crt_bundle_attach(void *cfg);
