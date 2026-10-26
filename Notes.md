# CYD Basketball Scores — Notes

The material that is not design or agent-brief: what the data source will and will not give you, the memory/TLS history behind the shipped shapes, the user-facing calibration walkthrough, and ideas parked on purpose. The full lessons-learned log lives in AGENTS.md.

## ESPN data caveats

- **College coverage is partial**: ESPN's public college scoreboard lists only the network-covered D1 games — ~10–19/day in conference season, 60+ on a November axis day — *not* the full ~350-game D1 slate (balldontlie's college endpoints exist but paywall the games).
- **No college preseason on the public feed** (probed 2026-10): every Jul–Oct day answered 0 events for both the 2025 and 2026 seasons, date-range queries too. The October exhibition season simply does not exist here — no client-side filter is hiding it. NBA October preseason *does* flow like any other slate. The dead-league forward hunt anchors the NCAA tabs on the earliest slate that exists (Nov 1 this year) and renders the scheduled games with their tip text.
- **Slate horizon**: ESPN publishes each slate ~2 weeks out, and the forward hunt only probes to +28 days.
- **Event stamps are UTC; the scoreboard's "day" is a US game day.** An evening US tip is already tomorrow in UTC, so the parser stamps game day = event stamp − 12 h (the same convention as `time_sync_game_day_epoch()`, and the day ESPN's `dates=` query is keyed on). `tools/sim` asserts the rule per fixture against glibc `timegm`.
- **ESPN pre-seeds games it hasn't played**: `"score":"0"`, `"period":0`, a `"0:00"` clock and `"TBD"` shortDetail on scheduled events. The parser forces scores/linescores to −1 outside `in`/`final` and prefers the tip text over the zeroed clock, or tomorrow's games render "0 - 0".
- **Busy days are ordered oldest-tip-first** (a November slate can list 69 events), so each day keeps its **24 newest** by capped sorted insertion — a plain append cap keeps the wrong half.
- **Body size**: the scoreboard response pads every event to ~20 KB (200–400 KB/day), which is why `espn_api` is a constant-memory streaming scanner with no cJSON tree anywhere. A keep-alive connection can also die **mid-body on a 200** — the client reports that as a failure, never as a (partial or empty) answer: a truncated day must not be banked as "answered empty" by the feed's known-empty set.

## Memory & TLS history (why the shipped shape looks like this)

- **The "cert storm" was ESP-side memory, not the CDN.** Clusters of `esp-x509-crt-bundle: PSA signature verification failed (-141)` read like broken edge certs; `-141` is `PSA_ERROR_INSUFFICIENT_MEMORY`, and IDF 6.0.2's `esp_crt_bundle.c` never checks `psa_hash_compute()`'s return, so an allocator failure prints as a crypto verdict. Below ~41 K free heap every host failed and **nothing self-healed** short of a reboot. `tools/psa_mre/` is a standalone minimal repro for the upstream report (deliberately untracked).
- **The fix shipped as a package**: burst keep-alive (one handshake per chain of requests, released when idle), the storm-floor restart (gated on `mbedtls_arena_active()`), the pre-warm heap gate, and above all the **44 KB mbedTLS static arena** (`main/mbedtls_arena.c`, `mbedtls_platform_set_calloc_free`): handshake system-heap cost 44.5 K → 10.9 K, 100/100 alternating ESPN/Google handshakes green at 14,360 B free heap where the stock allocator dies (~45.5 K wall). Measurements and the adopted-shape rationale: `tools/mbedtls_opt/README.md`. The arena's sizing caveat (tuned to the 2026-10 IDF/mbedTLS; a toolchain/TLS-config change moves the peak; MEMORY_E handshakes are the symptom) is written into `main/mbedtls_arena.c`'s header.
- **`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` must stay 16384**: 8192 passed 148/148 HEAD-probe handshakes and then failed the production GET — Akamai serves the scoreboard body in 16 KB records. 4096 breaks the handshake outright (`-0x0087`). `SSL_OUT_CONTENT_LEN=4096` is safe and kept. Lesson: a handshake-only soak certifies half the connection.
- **A parked keep-alive session is worse than reconnecting**: the open TLS session parks ~20–22 KB (IN/OUT record buffers + context), so the worker releases the connection before every sleep/backoff. Two artifacts to read logs with: heap read right after release looks alarmingly low (lwIP closes lazily; the ~29 K returns within seconds), and one connection per burst still leaks the known ~200 B.
- **lwIP drift ~200 B/connection** is not fixable from here; the 25 KB absolute floor turns the drift into roughly one self-healing reboot per week. That floor stays load-bearing with or without the arena.
- **The `s_buf` union clawback needed explicit bookkeeping**: blob staging aliases the parse page (the 4.4 KB that paid for 6-page boxes), and after the first fetch of a cycle the `.blob` view is junk — the fresh-skip once read a parse page as the header (`score cache is -54081193356277 s old`) and silently skipped refetch cycles. The mirror's identity (`s_blob_saved_at`, `s_buf_staged`) now lives outside the union, cleared at the single `svc_api_fetch` choke point. Full story in AGENTS.md Lessons.
- **wolfSSL spike** (branch `wolfssl-spike`, merged 2026-10): static-memory TLS proven on device (5/5 rounds, zero system heap touched; diet shape ~48 K net RAM), but the component was **removed from the build** — the arena already saves the memory, so the dependency is dead weight. Full experiment record + the IDF6 patch stay in `tools/wolfssl_port/`.

## Flash headroom

~1.77 MB of the 2 MB app partition, **~16% free**. Any new baked data (art, larger fonts, more logos) must be measured against `check_sizes.py` output first. Escape hatches: `tools/make_team_logos.py --all-conferences` toggle (the NCAA trim is ~256 KB of bitmaps), and the documented mbedTLS Kconfig diet in `tools/mbedtls_opt/README.md` (−24 K code / −75 K CMN bundle) if the partition ever crowds again.

## Touch calibration (user walkthrough)

Access: **Cards → gear → Settings → Touch Calibration**.

1. **Tap the crosshairs** in order (1 → 2 → 3 → 4): top-left, bottom-left, bottom-right, top-right.
2. Each tap records the raw XPT2046 ADC values and shows them in the centre panel.
3. **Long-press undo**: hold any crosshair ~1 s and release — LVGL's `LV_EVENT_LONG_PRESSED`, no timer needed.
4. When all 4 corners are collected, **Done & Save** appears; saving returns to Settings (boot flow: no valid calibration in NVS → the calibration screen runs before splash → cards).

Why the guard rails: **quadrant checking** (each tap must land in the expected half for X and Y — midpoint ~2047 on the 0–4095 ADC) catches mis-taps before they corrupt the matrix, and **500 ms debounce** keeps a fast double-tap from poisoning a corner. The loaded calibration is applied to the touch handler at boot and validated on load (range 0–4095, min/max sanity) before the saved flag is trusted. Per-panel orientation lives in the calibration data, not the driver's `swap_xy`/`mirror_*` flags (the driver applies those *after* the calibration callback and would flip finished screen coords) — a different panel needs recalibration, not flag changes. Reads are gated on PENIRQ (`CONFIG_XPT2046_INTERRUPT_MODE`); `CONFIG_XPT2046_Z_THRESHOLD` (default 400) is the knob if ghosts ever appear.

## Known issues / quirks

- `sdkconfig` may need `IDF_TARGET=esp32` (run `idf.py set-target esp32`).
- `idf.py` caches `-D` options in `build/CMakeCache.txt` — after a test build, the next plain `idf.py build` **keeps the hooks**; clear them with the empty `-DVAR=` form documented in AGENTS.md.
- If calibration data ever corrupts (e.g. `esptool merge-bin` not erasing NVS): `idf.py erase-flash`, or let the built-in validation reset it to defaults.

## Parking lot

- **Nov 1 opening-night soak** (and the multi-day arena heap soak): the busy multi-page slates exercise the 3-card pager + 18-game box for real. Tracked in AGENTS.md "What's Not Yet Done"; hooks already exist (`-DCYD_NBA_AUTOPAGE=1 -DCYD_NBA_AUTOPAGE_DEPTH=7 -DCYD_NBA_REFRESH_S=45`).
- **Compact game storage** (user idea, parked because the numbers do not demand it): store `(league, team ids, scores, date…)` + a side table for non-baked teams instead of ~368 B of `nba_game_t` strings — ~6× smaller statics, much deeper cache. Revisit if the cache ever needs weeks of history or more leagues (negative-id + linked-list encoding is the right shape then; names must survive for non-baked small-school teams).
- **A 7th cache page** needs a NEW clawback before it can be bought (status[24]→[16] + datetime[26]→[22] ≈ 1.5 KB, or parse page 24→20 — that one moves the same-day reachability wall). 6 pages is the parked sweet spot; full byte maths in AGENTS.md.
