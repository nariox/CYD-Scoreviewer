/* Static-memory arena for the mbedTLS/PSA allocator (spike-branch probe).
 *
 * Proves the mbedTLS-side answer to the "cert storm": mbedtls 4 routes ALL
 * of its (and TF-PSA's) allocations through the mbedtls_calloc function
 * pointer, and esp_config.h sets it as a *default value* (STD_CALLOC =
 * esp_mbedtls_mem_calloc -> heap_caps INTERNAL), not a hard macro - so
 * mbedtls_platform_set_calloc_free() is a live runtime hook. Pointing it at
 * a fixed .bss arena means a handshake can no longer fragment-fail inside
 * the system allocator; the storm class dies exactly like the wolfSSL
 * static-pool result, without replacing the TLS stack.
 *
 * Test prototype (first-fit + coalesce), gated by -DCYD_MBED_ARENA_KB=N.
 * All entry points are no-ops when the hook is not built.
 */
#pragma once

void mbedtls_arena_init(void);   /* call FIRST in app_main, before WiFi/TLS */
void mbedtls_arena_dump(const char *why);
/* True once the redirect is live. Consumers whose job was sized around the
   handshake-failure heap band (pre-warm gate, storm floor) retire that
   logic while this returns true: handshakes no longer touch system heap. */
bool mbedtls_arena_active(void);
