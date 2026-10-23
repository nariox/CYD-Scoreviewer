/* Static-memory arena for the mbedTLS/PSA allocator - spike-branch probe.
 * See mbedtls_arena.h for the why; this file is the how.
 *
 * Boundary-tag allocator (first-fit + coalescing) over one .bss buffer,
 * installed as mbedtls's global calloc/free. Threading: mbedtls runs under
 * esp_tls (http client task) AND wpa_supplicant paths, so every entry is
 * mutex-guarded. Built only with -DCYD_MBED_ARENA_KB=N.
 */
#include "mbedtls_arena.h"

#if defined(CYD_MBED_ARENA_KB) && CYD_MBED_ARENA_KB > 0

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "mbedtls/platform.h"

static const char *TAG = "mbedarena";

#define ARENA_BYTES ((size_t)CYD_MBED_ARENA_KB * 1024u)
#define BLK_HDR     16u  /* magic/size/prev/used - keeps payload 8-aligned */
#define BLK_MAGIC   0x41524d46u  /* "ARMF" */

typedef struct {
    uint32_t magic;
    uint32_t size;  /* payload bytes, multiple of 8 */
    uint32_t prev;  /* total block size (hdr+payload) of the previous block */
    uint32_t used;
} blk_t;

static uint8_t  s_arena[ARENA_BYTES] __attribute__((aligned(8)));
static uint32_t s_brk;      /* end of carved blocks (offset) */
static uint32_t s_used;     /* live payload bytes */
static uint32_t s_peak;
static uint32_t s_allocs, s_fails, s_foreign;
static bool     s_installed;
static SemaphoreHandle_t s_lock;

static blk_t *blk_next(blk_t *b)
{
    return (blk_t *)((uint8_t *)b + BLK_HDR + b->size);
}

static bool blk_valid(const blk_t *b)
{
    return (const uint8_t *)b >= s_arena &&
           (const uint8_t *)b + BLK_HDR <= s_arena + ARENA_BYTES &&
           b->magic == BLK_MAGIC;
}

static void *arena_calloc(size_t n, size_t size)
{
    size_t need = n * size;
    if (n != 0 && size != 0 && need / size != n) {
        return NULL; /* multiplication overflow */
    }
    need = (need + 7u) & ~(size_t)7u;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    void *res = NULL;
    uint32_t off = 0;
    uint32_t last_total = 0;   /* size of the block ending at `off` (0 = none) */
    while (off < s_brk) {
        blk_t *b = (blk_t *)(s_arena + off);
        if (!blk_valid(b)) break;
        if (!b->used && b->size >= need) {
            /* split when the tail can still host a header + 8 B block */
            if (b->size >= need + BLK_HDR + 8u) {
                blk_t *r = (blk_t *)((uint8_t *)b + BLK_HDR + need);
                r->magic = BLK_MAGIC;
                r->size = b->size - need - BLK_HDR;
                r->prev = BLK_HDR + need;
                r->used = 0;
                blk_t *nn = blk_next(r);
                if ((uint8_t *)nn < s_arena + s_brk && blk_valid(nn)) {
                    nn->prev = BLK_HDR + r->size;
                }
                b->size = need;
            }
            b->used = 1;
            memset(b + 1, 0, b->size);
            s_used += b->size;
            if (s_used > s_peak) s_peak = s_used;
            s_allocs++;
            res = b + 1;
            break;
        }
        last_total = BLK_HDR + b->size;
        off += last_total;
    }
    if (!res && s_brk + BLK_HDR + need <= ARENA_BYTES) {
        blk_t *b = (blk_t *)(s_arena + s_brk);
        b->magic = BLK_MAGIC;
        b->size = (uint32_t)need;
        b->prev = last_total;  /* blocks are contiguous: last ends at s_brk */
        b->used = 1;
        memset(b + 1, 0, b->size);
        s_used += b->size;
        if (s_used > s_peak) s_peak = s_used;
        s_allocs++;
        res = b + 1;
        s_brk += BLK_HDR + (uint32_t)need;
    }
    if (!res) {
        s_fails++;
        ESP_LOGE(TAG, "arena full: need %u B (used %u, peak %u)",
                 (unsigned)need, (unsigned)s_used, (unsigned)s_peak);
    }
    xSemaphoreGive(s_lock);
    return res;
}

static void arena_free(void *p)
{
    if (!p) return;
    blk_t *b = (blk_t *)((uint8_t *)p - BLK_HDR);
    if (!blk_valid(b) || !b->used) {
        /* Foreign pointer: ESP32's ROM mbedtls glue (mbedtls_rom_osi.c)
         * binds ._mbedtls_calloc to MBEDTLS_PLATFORM_STD_CALLOC at compile
         * time, so ROM-side bignum temporaries come from the system heap yet
         * are freed through this runtime hook. Pair them back with free()
         * (same single internal heap) - rejecting them leaked ~1.3 KB per
         * handshake (capture B2). The reverse mismatch (arena ptr into
         * libc free) never occurs: ROM objects are freed by the ROM glue. */
        s_foreign++;
        if (s_foreign <= 3) {
            ESP_LOGW(TAG, "foreign free #%u %p -> libc free", (unsigned)s_foreign, p);
        }
        free(p);
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    b->used = 0;
    s_used -= b->size;

    /* coalesce with the next block when it is free */
    blk_t *nb = blk_next(b);
    if ((uint8_t *)nb < s_arena + s_brk && blk_valid(nb) && !nb->used) {
        b->size += BLK_HDR + nb->size;
    }
    blk_t *nnb = blk_next(b);
    if ((uint8_t *)nnb < s_arena + s_brk && blk_valid(nnb)) {
        nnb->prev = BLK_HDR + b->size;
    }
    /* coalesce with the previous block when it is free */
    if (b->prev) {
        blk_t *pb = (blk_t *)((uint8_t *)b - b->prev);
        if (blk_valid(pb) && !pb->used) {
            pb->size += BLK_HDR + b->size;
            blk_t *n3 = blk_next(pb);
            if ((uint8_t *)n3 < s_arena + ARENA_BYTES && blk_valid(n3)) {
                n3->prev = BLK_HDR + pb->size;
            }
        }
    }
    xSemaphoreGive(s_lock);
}

void mbedtls_arena_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    mbedtls_platform_set_calloc_free(arena_calloc, arena_free);
    s_installed = true;
    ESP_LOGI(TAG, "mbedtls allocator redirected to %u KB arena @ %p",
             (unsigned)CYD_MBED_ARENA_KB, s_arena);
}

bool mbedtls_arena_active(void)
{
    return s_installed;
}

void mbedtls_arena_dump(const char *why)
{
    ESP_LOGI(TAG, "%s: arena used %u B, peak %u B, allocs %u, fails %u, foreign %u (%u KB total)",
             why, (unsigned)s_used, (unsigned)s_peak,
             (unsigned)s_allocs, (unsigned)s_fails, (unsigned)s_foreign,
             (unsigned)CYD_MBED_ARENA_KB);
}

#else /* !CYD_MBED_ARENA_KB */

void mbedtls_arena_init(void) {}
void mbedtls_arena_dump(const char *why) { (void)why; }
bool mbedtls_arena_active(void) { return false; }

#endif
