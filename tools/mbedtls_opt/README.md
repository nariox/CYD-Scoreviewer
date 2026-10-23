# mbedTLS optimization spike (M1 Kconfig diet + M2 arena redirect)

The other half of the `wolfssl-spike` exploration: the "cert storm" class
(handshakes failing with PSA out-of-memory below ~45.5 K free heap) is rooted
in the stock allocator being used *during* TLS. wolfSSL proved a static pool
kills it (`tools/wolfssl_port/`); this doc measures what the **shipped**
mbedTLS stack can do about the same problem, since mbedTLS stays linked
regardless (Wi-Fi WPA crypto is its job).

Measured shapes (all device-verified on `/dev/ttyUSB0`, ESP-IDF 6.0.2,
captures `/tmp/opencode/mbed_dev{A,B,B2,B2b,C3,M3}.log`):

| shape | binary B | notes |
|---|---|---|
| production stock (hook-free) | **1,741,232** | the true flash baseline. (The storm capture ran on a stress-hook build — see the GC landmine in AGENTS.md: hook builds dead-code the feed path, so A measured 1,723,280.) |
| A — stock + TLSSTRESS, storm capture | — | ESPN (RSA chain) fails first at **46,344 B** free (probe 56/100), Google (ECDSA) survives longer; handshake transient bottoms the system heap to **1.6–10 K** window-min; drift ≈ −230 B/probe |
| C — A + code diet | 1,717,312 | **−23,920 B** (30+ features + CLIENT_ONLY, on top of the stock `-Os`) |
| C2 — C + CMN bundle | 1,666,528 | **−74,704 B** (FULL→CMN bundle alone: −50,784) |
| C3 — C + ISRG-only bundle | **1,651,472** | **−89,760 B** (CMN→ISRG: −15,056); device-verified: full feed + 4-league pre-warm green (all ESPN, ISRG chain) |
| M2 — A + 64 K arena (LVGL-32 test shape) | +1,072 B arena code | **storm killed**: 100/100 probes while passing through the whole 46 K wall where A dies; arena peak **36,304 B** (22,575 allocs, 0 fails); per-handshake system-heap cost **44.5 K → 10.9 K** |
| M3 — C3 + 48 K arena + stress | — | diet+arena coexist: ESPN **30/30** green, peak **36,264 B** (≈ M2 — bundle swap doesn't move the transient); Google **0/30** — see bundle trade |

## M1 — Kconfig diet (exact list)

Applied to `sdkconfig` (would go into `sdkconfig.defaults` if adopted).
Kept deliberately: SHA1+AES (WPA2), SHA256/384 (TLS + SAE), P-256/384/521 +
CURVE25519, ECDSA verify (ECDSA roots in bundles), ECDH, RSA (v15), GCM/CCM/
CBC/CTR, PEM-free X509/ASN1 parse, CTR_DRBG + ENTROPY, SELF_TEST, THREADING,
CROSS_SIGNED_VERIFY, IN 16384 / OUT 4096 (**IN must stay 16384** — see the
HEAD-vs-GET lesson in AGENTS.md).

Off (role): `MBEDTLS_TLS_SERVER_AND_CLIENT` → `MBEDTLS_TLS_CLIENT_ONLY`
(plus the raw `MBEDTLS_TLS_SERVER` line commented).

Off (TLS surface): `SSL_CACHE_C`, `SSL_RENEGOTIATION`, `SSL_ALPN`,
`SSL_MAX_FRAGMENT_LENGTH`, `SSL_ALL_ALERT_MESSAGES`,
`CLIENT_SSL_SESSION_TICKETS`, `KEY_EXCHANGE_RSA` (static-RSA kx).

Off (crypto/cert fat): `CIPHER_MODE_{CFB,OFB,XTS}` (CTR stays for GCM;
CCM/GCM stay for TLS+wifi), `MD5_C` + `ROM_MD5`, `SHA512_C` (SHA384_C
stays — note: this drops SHA-512 sigs and SAE-over-P-521), `ECDSA_
DETERMINISTIC` + `HMAC_DRBG_C`, `PKCS5_C`, `PKCS7_C`, `PKCS1_V21` +
`X509_RSASSA_PSS_SUPPORT` (ESPN's chain is PKCS1#1.5; PSS servers would
fail — flagged in the recommendation), `ECP_DP_{SECP256K1,BP256R1,BP384R1,
BP512R1}`, `GCM_SUPPORT_NON_AES_CIPHER`, `PK_PARSE_EC_{EXTENDED,COMPRESSED}`.

Off (I/O + strings): `FS_IO`, `PEM_PARSE_C` (the crt bundle is DER),
`PEM_WRITE_C`, `X509_CRL_PARSE_C`, `X509_CSR_PARSE_C`, `ERROR_STRINGS`,
`VERSION_C`.

**Landmines hit (do not re-learn these the slow way):**
- `wpa_supplicant/crypto/crypto_mbedtls-{rsa,ec}.c` call
  `mbedtls_pk_write_{pubkey_der,key_der}` **unguarded** → `PK_WRITE_C` +
  `ASN1_WRITE_C` MUST stay.
- `esp-tls/esp-tls-crypto/esp_tls_crypto.c` calls `mbedtls_base64_encode`
  unconditionally → `BASE64_C` MUST stay (PEM_PARSE can still go).
- `X509_TRUSTED_CERT_CALLBACK` and `CMAC_C` are re-selected by kconfig
  (esp-tls / wpa) — setting them off is a no-op.
- The TLS-role **choice silently reverts** to SERVER_AND_CLIENT if a build
  fails mid-reconfigure: verify with `grep TLS_CLIENT_ONLY sdkconfig` after
  reconfigure; re-apply + `idf.py reconfigure` if it flipped.
- The code diet totals **−23.9 K** (re-measured against the hook-free
  production binary, 1,741,232 B — an earlier −6 K figure compared against a
  GC-shrunken stress build): meaningful, but mbedTLS already compiles `-Os`
  (`MBEDTLS_COMPILER_OPTIMIZATION_SIZE`), so the bundle remains the big dial.

Bundle stages: `DEFAULT_FULL` → `DEFAULT_CMN` (−50.8 K, keeps ~12 common
roots incl. ISRG) → `DEFAULT_NONE` + `CUSTOM_CERTIFICATE_BUNDLE` pointing at
`certs_isrg.pem` (`tools/mbedtls_opt/certs_isrg.pem`, set as the bundle path
in `sdkconfig`; ISRG Root X1 + X2 from letsencrypt.org) for
another −15 K. **Trade (measured, M3): an ISRG-only bundle cannot verify
Google's GTS chain** — `esp-x509-crt-bundle: No matching trusted root
certificate found` → `-0x3000`, every Google probe fails while ESPN stays
green. Production only talks to ESPN so this is survivable, but it also
blinds the `CYD_NBA_TLSSTRESS` two-host diagnostic and hard-couples the
product to Let's Encrypt. Flash is NOT the constraint (78.8% full at C3, vs
83.0% stock) —
recommend code diet + keep FULL (or CMN); ship ISRG-only only on purpose.

## M2 — arena redirect (`main/mbedtls_arena.c/h`)

Feasibility: `mbedtls/port/include/mbedtls/esp_config.h` defines
`MBEDTLS_PLATFORM_MEMORY` with `MBEDTLS_PLATFORM_STD_CALLOC =
esp_mbedtls_mem_calloc` as a **function-pointer default** (not a pinned
macro), so `mbedtls_platform_set_calloc_free()` at boot redirects *all*
library + PSA allocations (PSA core allocates through `mbedtls_calloc` too).

Prototype: `CYD_MBED_ARENA_KB=N` (see `main/CMakeLists.txt`) compiles a
mutex-guarded first-fit + coalesce allocator over one static arena, installed
as the **first call in `app_main`** (before Wi-Fi/TLS; the bootloader and
anything before `app_main` is unaffected). `mbedtls_arena_dump()` logs
used/peak/allocs/fails/foreign and is wired into the TLSSTRESS summary lines
(+ a 15 ms windowed min-free sampler, so A/B runs share the wolfSSL-capture
metric). TLSSTRESS builds also skip `svc_cache_load()` — the early NVS board
render would overflow the LVGL-32 pool of the arena-test shape (screens alone
sit at 93% of it).

Findings:

1. **True mbedTLS+PSA TLS working set ≈ 36 K** (peak 36,304 FULL bundle /
   36,264 diet, byte-stable). Production arena sizing: ~40 K + WPA
   coexistence margin (rekey/PMF allocations land in the arena too; a
   WPA2-PSK-SHA256+PMF link ran green throughout).
2. **ROM glue bypasses the runtime hook**: `port/mbedtls_rom/mbedtls_rom_osi.c`
   binds `._mbedtls_calloc`/`._rom_mbedtls_mem_calloc` to
   `MBEDTLS_PLATFORM_STD_CALLOC` at **compile time**, so ~3 pointers per
   handshake (~430 B each, both hosts — bignum temps for the verify ops)
   come from the system heap yet are freed *through the hook*. Rejecting
   them (first attempt) leaked ~1.3 KB/probe and killed the run at probe ~56
   with heap-starved failures. The fix is to **pair them back with libc
   `free()`** (single internal heap here): `s_foreign` counter in the dump,
   zero leak after. The reverse mismatch (arena pointer freed by the ROM
   glue) never occurred and the range check makes corruption impossible.
3. **Fit verdict**: production-shape `.bss` tops out at **~34.5 K** of arena
   (48 K version: `region dram0_0_seg overflowed by 14680 bytes`), below the
   ~40 K need — and carving from the boot heap instead lands steady free heap
   under the 25 K floor. **M2 in production needs ~40 K freed elsewhere
   first** — the same RAM-diet conversation the wolfSSL pool requires (LVGL
   pool not dietable; candidates: page boxes, cache pages, task stacks).
4. **Drift is not the TLS library**: −206 B/probe with the arena vs −230
   stock — the known ~200 B/connection creep lives in esp_http_client/lwIP,
   so the shipped floors (46 K storm floor / 48 K pre-warm gate / 25 K
   absolute) only relax with respect to the *storm*, not the drift.

## Reproducing a measurement build

```bash
# A-style baseline capture (production shape):
idf.py -DCYD_MBED_ARENA_KB= -DCYD_NBA_TLSSTRESS=100 build   # + LVGL 64 in sdkconfig
# arena run (before the 3-card trial shape, a >=40K arena only linked with
# the LVGL-32 test shape — see the production-shape trial below):
idf.py -DCYD_MBED_ARENA_KB=64 -DCYD_NBA_TLSSTRESS=100 build
# diet verification (production shape, no hooks):
idf.py -DCYD_MBED_ARENA_KB= -DCYD_NBA_TLSSTRESS= build
```

`CYD_MBED_ARENA_KB` also gates a no-op stub, so `mbedtls_arena_init()` /
`mbedtls_arena_dump()` are safe to call from production builds.

## M2 in production shape — the 3-card trial (blocker cleared, device-proven)

The old blocker — a 40 K arena does not fit production `.bss` (linker
ceiling ~34.5 K) — is paid for by the smaller page, using build knobs
(`CYD_NBA_BOX_GAMES` / `CYD_NBA_PERSIST_GAMES` are `#ifndef` overrides in
`nba_service.c`):

```bash
idf.py -DCYD_NBA_PAGE_SIZE=3 -DCYD_NBA_BOX_GAMES=12 \
       -DCYD_NBA_PERSIST_GAMES=12 -DCYD_MBED_ARENA_KB=44 build
# + CONFIG_LV_MEM_SIZE_KILOBYTES=48 in sdkconfig (trial knob, not defaults:
#   a 3-card page uses 75% of the smaller pool — the same comfort ratio as
#   8 cards at 64, so the LVGL pool is partially dietable now)
```

Savings: game cache 32→12 (-7.4 K), page boxes 64→48 (-5.9 K), LVGL pool
-16 K, minus persist 8→12 games (+1.5 K; old 8-game blobs self-reject on
length and are rewritten) ⇒ **44 K arena links with ~21 K of linker
margin**, boot heap 37.7 K, flash 1,741,408 B (+176 vs production, 17%
free). 3 x 46 px + 2 x 8 px = 154 px inside the 164 px card band, so
pages never scroll.

Device captures:

| capture | shape | result |
|---|---|---|
| `m2p_devA` | boot + AUTOPAGE + PAGEMON | 3 cards, pool 75%, heap flat ~36.7 K across 250 s; cached-page paints **211/268/308 ms** (min/med/max, n=862 — fixed path dominates, card count barely matters) |
| `m2p_devB` | TLSSTRESS=100 + shape anchor | boot 41.6 K (already under the stock 46.3 K wall), **100/100** both hosts, worst streak 0, probe-min heap **14,360**, window-min 4,176; arena peak 36,304 / 112,803 allocs / 0 fails, 3 foreign pairs/probe |
| `m2p_devC2` | `CYD_TEST_NO_WIFI=1` (backend retry timer connects at 66 s) | offline: saved-data-only board, 15/30/60 s backoff ladder, no pre-warm; on connect: immediate wake from backoff, pre-warm fires only then |
| `m2p_devC1` | online boot | blob @1.5 s → fresh board @8 s → WNBA/NCAAB/NCAAW boxes ready by 82 s; zero defer lines, zero storm lines |

Two consequences are baked in with the shape: the stock heap floors are
gated on `mbedtls_arena_active()` (the pool sits below the storm band
permanently once the arena is .bss — the 48 K pre-warm gate would defer
forever, the 46 K storm floor would reboot-loop an ordinary outage), and
the pre-warm became network-bound: the worker starts at boot (an
unreachable saved AP still paints the NVS board), the CONNECTED callback
pokes it out of backoff, the once-per-boot warm slot is consumed only on
a *connected* success, and leagues whose box is inside the 6 h freshness
rule are skipped (`pre-warm: <lg> box still recent - skipped`).

The TLSSTRESS binary at this shape REQUIRES the volatile-store shape
anchor at the top of `svc_tls_stress` (AGENTS Lessons: with the feed
unreachable GCC promotes the pager arrays to `.rodata` and the probes run
on a fake +35 K of heap).
