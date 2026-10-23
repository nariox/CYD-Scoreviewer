# wolfSSL 5.8.2~1 -> ESP-IDF 6.0.2 port patch

The `wolfssl/wolfssl` managed component (registry, uploaded ~1 yr ago) does not
build against IDF 6.0.2 out of the box. This patch is the minimal set of edits
that makes it build for this project's use (client TLS over BSD sockets, software
crypto). Apply it **after** the component manager downloads/extracts the
component and **before** building:

```bash
cd cyd_basketball_scores                       # after any `idf.py reconfigure`
                                               # or full `fullclean` re-download
cd managed_components/wolfssl__wolfssl
patch -p1 < ../../../tools/wolfssl_port/wolfssl-5.8.2-idf6.patch
```

`managed_components/` is gitignored, so the patch must be re-applied after
anything that re-downloads the component. (Verified against the extracted
archive cached as `wolfssl__wolfssl_5.8.2_fde52713` in the Component Manager
cache.)

## What the patch changes and why

`CMakeLists.txt`

1. **`COMPONENT_REQUIRES` += `esp_netif esp_hal_security esp_hal_clock`** —
   IDF 6 split the `hal/` headers out of `hal`/`esp_hw_support` into new
   components (`esp_hal_security`: `sha_hal.h`/`sha_ll.h`; `esp_hal_clock`:
   `clk_gate_ll.h`), which the ESP32-classic HW-accel glue includes. `esp_netif`
   was already included un-declared (configure-time validation warning).
2. **Drop `wolfssl/wolfcrypt/port/Espressif` from `COMPONENT_ADD_INCLUDEDIRS`** —
   that flat dir exports wolfSSL's own `esp_crt_bundle.h`, which *hijacks* IDF's
   `<esp_crt_bundle.h>` for every component in the build once wolfSSL is a
   dependency (our `espn_api.c` silently loses `esp_crt_bundle_attach`). Only
   wolfSSL's optional esp-tls-mode bundle port uses that header; not compiled
   here.
3. **Exclude `wolfcrypt/src/port/Espressif/esp_sdk_mem_lib.c`** — references
   linker-script symbols IDF 6 removed from the ESP32-classic script (`rtc_data`,
   `user_data_ram`, ...). The file only provides the debug `sdk_meminfo()`
   helper; nothing else in the build calls it.

`include/user_settings.h` (ESP32-classic branch + global)

4. **`NO_ESP32_CRYPT` + the three individual `NO_WOLFSSL_ESP32_CRYPT_{HASH,AES,RSA_PRI}`** —
   `esp32_mp.c`/`esp32_sha.c` drive `periph_module_enable(PERIPH_RSA_MODULE)` etc.
   directly; IDF 6 removed those enums and moved the clock gating behind
   `esp_hal_clock`, so the HW-accel glue does not compile. Building with all
   four macros makes every HW path compile to nothing (software crypto).
   Note: the wolfSSL *core* (sha.c/sha256.c/integer.c cleanup blocks) keys off
   the INDIVIDUAL macros, not the umbrella, so all four are required.
   TODO for a productized port: teach the glue the new clock API and get RSA/SHA
   acceleration back (CPU-time win, not a heap win).
5. **`HAVE_SNI` + `HAVE_TLS_EXTENSIONS`** — stock settings only define SNI under
   the esp-tls-integration branch (inactive here, and that integration itself is
   a pre-IDF-6 leftover). Akamai vhosts on SNI; mbedTLS always sends it; the
   probe must too.
6. **`DEBUG_WOLFSSL_MALLOC` off** — routes every allocation through
   `wc_debug_pvPortMalloc` in the excluded `esp_sdk_mem_lib.c` (link error), and
   adds per-allocation bookkeeping that would skew the spike's heap measurements
   vs the mbedTLS baseline.
7. **`FP_MAX_BITS (4096*2)`** — ESPN's leaf chain verifies up through an RSA-4096
   root (`ISRG Root YR` cross-signed by X1); TFM's default `FP_MAX_BITS=4096`
   tops out at 2048-bit math and the verify fails. Same widening the component's
   esp-tls recipe applies. Side effect that drives ALL pool math: every `fp_int`
   becomes 1064 B, so `RsaKey` 5348 / `ecc_key` 4288 / `ecc_point` 3196.
8. **`WOLFSSL_STATIC_MEMORY`** — the feature the spike exists to measure. Ships
   inside `src/internal.c` for 5.8.2 (this tree has no `mem_mgr.c`). Load TWO
   regions or die mid-connect: `WOLFMEM_GENERAL` (handshake objects) +
   `WOLFMEM_IO_POOL` (the 2×16992 B record buffers live ONLY in IO regions).
   Pass `maxSz = 0` as the 6th arg — it is a *concurrency limit*; `1` arms a
   MEMORY_E reject gate in `internal.c`. Bucket exhaustion never falls back to
   malloc, so sizing is exact science (see the alloc-trace notes in
   `main/wolfssl_spike.c`).
9. **`WOLFMEM_BUCKETS` / `WOLFMEM_DIST`** — the `OPENSSL_EXTRA` default bucket
   list (one 30400 bucket) starves everything else: greedy top-down carving
   spends the whole region on one block and a 128-byte RNG seed fails. The
   baked list is tuned to the measured live set; verify any retune on the host
   BEFORE flashing with a probe that runs the real `wc_LoadStaticMemory` carve.

## Client-configuration notes (X25519 pin)

TLS 1.2 negotiates X25519 with Akamai on its own; **TLS 1.3 does not**: its
ClientHello keyshare defaults to P-256 (`preferredGroup[0]` in `src/tls.c`), and
the TFM scalar mul wants ~10 concurrent 3.2 KB `ecc_point`s (base + `M[8]`
window + tG) — tens of KB of pool for one handshake step. Pinning the group to
X25519 (curve25519 math is stack-based, zero pool objects) makes 1.3 as cheap
as 1.2. Two calls are required: `wolfSSL_UseSupportedCurve()` only restricts the
*advertised* groups; the keyshare default is picked unless `wolfSSL_UseKeyShare()`
has already seeded the entry.

`wolfSSL_SetGlobalHeapHint()` (route wolfCrypt's NULL-hint scratch into the
pool) was tried and **rejected**: with a per-connection CTX lifecycle, objects
allocated before the pool load and freed after a hint change hit
`assert failed: heap_caps_free "free() target pointer is outside heap areas"`
(boot loop). The scratch is small and transient (~5 K peak); leave it on the
native heap. A Gate-2 bridge with one never-freed CTX could revisit it.

## Memory diet layer (optional, inside the patch's `user_settings.h` EOF block)

The patch ships a **DIET profile** after the port items: `NO_ED25519` +
`WOLFSSL_SHA512` undef (they come from the stock `MY_USE_ECC` menu; undef at
EOF wins), `NO_WOLFSSL_SERVER`, `NO_MD5`, `NO_DSA`, `NO_SHA`,
`NO_ERROR_STRINGS`, `WOLFSSL_SMALL_STACK`. Device-proven (capture 14):
**−114 KB flash** vs the undieted component, ESPN's real RSA chain still
verifies, Wi-Fi untouched (WPA crypto is mbedTLS's job, not wolfSSL's).
`WOLFSSL_SMALL_STACK` (capture 15) moves the big handshake locals into the
pool — TLS task stack high-water came down to **2,652 B of 24,576**, so a
production TLS task can run ~8 K instead of 24 K (≈16 K of DRAM diet,
flash-neutral). `NO_ECC` is **not possible** in 5.8.2 (`internal.c` uses
`peerEccKey` unguarded when `HAVE_CURVE25519` is on — upstream gap).
**SP math rejected** (capture 16i): +22 KB flash and ~20% slower handshakes;
`sp_int`(4096) ≈ `fp_int` size, so no pool win either — and note the landmine:
`WOLFSSL_SP_MATH_ALL` without `WOLFSSL_HAVE_SP_RSA` auto-derives
`SP_INT_BITS 3072` and the RSA-4096 root verify dies `MEMORY_E`.

## App-side notes (live in git, not in this patch)

- `main/wolfssl_spike.c` must `#define WOLFSSL_USER_SETTINGS` before including
  `wolfssl/ssl.h` (the component only sets that flag for its own sources).
- `idf.py -DCONFIG_ESP_WOLFSSL_NO_STACK_SIZE_BUILD_WARNING=y` — the stock
  settings `#warning` that the *main task* stack is < 10.5 KB is fatal under
  IDF 6's `-Werror`; the probe runs in its own task. (Set in
  `sdkconfig.defaults`.)
- Pool sizing lives in `main/wolfssl_spike.c`: **gen 44 KB + a single
  16,992 B IO slot = 64 KB prepay** (was 80 KB: gen 44 + two IO slots). The
  capture-11 alloc-trace replay proved the IO peak is never >1 concurrent
  buffer across 5 rounds / both TLS versions / full 263 KB bodies, and the
  gen region's live-set peak is 20.8 KB (the ~40 KB floor is the DIST's
  block-carve floor, not demand). Capture-17's `inUse` polling against the
  pool struct was abandoned — buf+0 is a template copy and reads garbage;
  replay the alloc trace instead.

## Known-good build

`idf.py -DCYD_WOLFSSL_SPIKE=5 build` on IDF 6.0.2 / esp-15.2.0, ESP32-classic.
Diet + SMALL_STACK + retuned pools: binary **1,405,216 B**. Device-verified
(capture 18): 5/5 rounds green on the 64 KB prepay — TLS 1.2 ×3 and TLS 1.3 ×2
(group pinned to X25519), handshakes 245–491 ms, full 262 KB scoreboard
bodies; heap @ round start 53,408 (+16.4 K returned by the pool retune),
min-free mid-body 35,056 (the ~17 K lwIP transient stays — it is not TLS
library memory), post-teardown **flat at 52,580, zero drift**; stack HWM
2,652 B.

**Flash-number caveat:** the spike-shape binaries (1,405,216 / 1,518,912)
understate a production+wolfSSL build — with the spike define active,
`svc_tls_stress`-style never-returning entry points let GCC dead-code the
feed/render path and linker GC drops ~363 KB with it (logos included; the
same effect retroactively explains why the old "parallel link" number came in
204 KB below production). A production-shape flash number for the bridge is a
Gate-2 measurement. (An earlier Gate-1 note of "~2.03 MB, doesn't fit" was
measured on a `DEBUG_WOLFSSL`/`WOLFSSL_DEBUG_MEMORY` diagnostic build.)
