#pragma once
/* Host shim: only the auth-mode enum tag is referenced by wifi_backend.h. */
typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA2_PSK = 3,
} wifi_auth_mode_t;
