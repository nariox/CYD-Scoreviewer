#pragma once
/* Host shim: no real heap accounting in the preview harness. */
static inline unsigned int sim_free_heap(void) { return 100000u; }
#define esp_get_free_heap_size() sim_free_heap()
