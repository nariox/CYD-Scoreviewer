#pragma once
#include <stdint.h>

/* Host shim: the transport-failure diagnostics in espn_api.c / time_sync.c
   log the largest free internal block. That code path never runs against
   fixtures (no transport on the host), so any value compiles and is unused. */
#define MALLOC_CAP_INTERNAL 0x4000U

static inline uint32_t heap_caps_get_largest_free_block(uint32_t caps)
{
    (void)caps;
    return 0;
}
