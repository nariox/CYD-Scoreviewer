idf_component_register(SRCS "app_main.c" "splash_screen.c" "cards_screen.c" "game_detail_screen.c" "team_logos.c" "league_logos.c" "team_venues.c" "pro_team_ids.c" "basketball_img.c" "touch_integration.c" "settings_screen.c" "wifi_screen.c" "touch_test_screen.c" "calibration_screen.c" "lcd.c" "nvs_settings.c" "wifi_backend.c" "espn_api.c" "nba_service.c" "time_sync.c" "mbedtls_arena.c"
                      INCLUDE_DIRS ".")

# Optional short refresh interval for soak-testing the score feed worker:
#   idf.py -DCYD_NBA_REFRESH_S=60 build      (short interval)
#   idf.py -DCYD_NBA_REFRESH_S= build        (back to the default)
# (DEFINED matters: an undefined variable compares as its literal name in
# if(), so the bare NOT/STREQUAL form is true before the cache has ever
# seen it - which would emit an empty define and break the build.)
if(DEFINED CYD_NBA_REFRESH_S AND NOT CYD_NBA_REFRESH_S STREQUAL "")
    # Quotes matter: unquoted parentheses are CMake list syntax and the define
    # gets split apart (the build then dies in the shell with a stray `-D)`).
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "NBA_SERVICE_REFRESH_MS=(${CYD_NBA_REFRESH_S}*1000)")
endif()

# Cards per page (default 8):
#   idf.py -DCYD_NBA_PAGE_SIZE=12 build
if(CYD_NBA_PAGE_SIZE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SVC_PAGE_SIZE=${CYD_NBA_PAGE_SIZE})
endif()

# Page-box depth (games preloaded per league tab, default 2 pages) and NVS
# snapshot depth (default 1 page), decoupled from the page size for the
# 3-card RAM-diet trial (12 preloaded = 4 instant pages per tab):
#   idf.py -DCYD_NBA_BOX_GAMES=12 -DCYD_NBA_PERSIST_GAMES=12 build
if(DEFINED CYD_NBA_BOX_GAMES AND NOT CYD_NBA_BOX_GAMES STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SVC_BOX_GAMES=${CYD_NBA_BOX_GAMES})
endif()
if(DEFINED CYD_NBA_PERSIST_GAMES AND NOT CYD_NBA_PERSIST_GAMES STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SVC_PERSIST_GAMES=${CYD_NBA_PERSIST_GAMES})
endif()

# Measurement hook: stamp-to-pixel latency of each page step (pair with
# -DCYD_NBA_AUTOPAGE=1 to drive the pager without a finger on the glass):
#   idf.py -DCYD_NBA_PAGEMON=1 -DCYD_NBA_AUTOPAGE=1 -DCYD_NBA_REFRESH_S=45 build
if(CYD_NBA_PAGEMON)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CYD_NBA_PAGEMON)
endif()

# Soak hook: the worker pages through history on its own, so the paging paths
# get exercised on device without touching the screen:
#   idf.py -DCYD_NBA_AUTOPAGE=1 -DCYD_NBA_REFRESH_S=45 build
if(CYD_NBA_AUTOPAGE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CYD_NBA_AUTOPAGE)
endif()

# Soak hook: open the game detail view after the first page is rendered, to
# exercise the LVGL pool handover without touching the screen:
#   idf.py -DCYD_NBA_AUTODETAIL=1 -DCYD_NBA_REFRESH_S=45 build
if(CYD_NBA_AUTODETAIL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CYD_NBA_AUTODETAIL)
endif()

# Soak hook: hop to the college feed and back after the first cycles, to
# exercise the tab switch + college pre-warm take without touching the screen:
#   idf.py -DCYD_NBA_AUTOTAB=1 -DCYD_NBA_REFRESH_S=45 build
# Level 2 = race mode: flip the feed from a timer while the first cycle is
# still fetching - the exact interleaving that used to mix two leagues on one
# board. Expect a "switch pending" drop, then a clean single-league board:
#   idf.py -DCYD_NBA_AUTOTAB=2 -DCYD_NBA_REFRESH_S=45 build
if(CYD_NBA_AUTOTAB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CYD_NBA_AUTOTAB=${CYD_NBA_AUTOTAB})
endif()

# Test hook: start the board on a college feed (2 = MBB, 3 = WBB), so the
# ESPN path is exercised without touching the screen:
#   idf.py -DCYD_TEST_LEAGUE=2 build
if(DEFINED CYD_TEST_LEAGUE AND NOT CYD_TEST_LEAGUE STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "CYD_TEST_LEAGUE=(${CYD_TEST_LEAGUE})")
endif()

# Test hook: shift the feed's "today" by N days, so a board can be flashed
# against real game days in an off-season (college in October):
#   idf.py -DCYD_TEST_DAY_SHIFT=-286 build
# (DEFINED matters: an *undefined* variable compares as its own literal name
# in if(), so `NOT X STREQUAL ""` alone would be true and emit an empty
# "-DCYD_TEST_DAY_SHIFT=()" define.)
if(DEFINED CYD_TEST_DAY_SHIFT AND NOT CYD_TEST_DAY_SHIFT STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "CYD_TEST_DAY_SHIFT=(${CYD_TEST_DAY_SHIFT})")
endif()

# Soak hook: hammer fresh TLS handshakes, alternating ESPN (Akamai/RSA) and
# Google (ECDSA), to hunt the PSA -141 "cert storm" latch on demand:
#   idf.py -DCYD_NBA_TLSSTRESS=400 build && idf.py -p /dev/ttyUSB0 flash
# (~40 min; the score feed does NOT run while it hammers - clear the hook
# afterwards.) A fail streak spanning both hosts = device-side, proven.
if(DEFINED CYD_NBA_TLSSTRESS AND NOT CYD_NBA_TLSSTRESS STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CYD_NBA_TLSSTRESS=${CYD_NBA_TLSSTRESS})
endif()

# mbedTLS static arena (M2 storm-killer, device-proven production shape):
# redirects the whole mbedtls/PSA allocator into a fixed .bss arena via the
# runtime hook, so handshakes stop touching the system heap (the cert-storm
# class dies at the root). ON by default at the measured 44 K sizing (TLS
# peak 36,304 B + headers + WPA margin); opt out with an empty value:
#   idf.py -DCYD_MBED_ARENA_KB= build
# A custom size is the 3-card-shape territory only: production .bss leaves
# ~65 K of arena ceiling (page 3 + boxes 12 + LVGL pool 48 fund it).
set(CYD_MBED_ARENA_KB "44" CACHE STRING "mbedTLS static arena in KB (empty = stock allocator)")
if(DEFINED CYD_MBED_ARENA_KB AND NOT CYD_MBED_ARENA_KB STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CYD_MBED_ARENA_KB=${CYD_MBED_ARENA_KB})
endif()

# wolfSSL spike removed 2026-10 (component, probe task and CYD_WOLFSSL_SPIKE
# hook): the arena shipped instead. Record: tools/wolfssl_port/.

# Test hook: boot with no network attempt (autoconnect skipped), to verify
# the saved-data-only offline experience: the NVS board must still paint,
# the pre-warm must not fire, and fetch attempts must sit in backoff:
#   idf.py -DCYD_TEST_NO_WIFI=1 build && idf.py -p /dev/ttyUSB0 flash
if(CYD_TEST_NO_WIFI)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CYD_TEST_NO_WIFI)
endif()

# If defaults_override.h exists, define CONFIG_DEFAULTS_OVERRIDE so nvs_settings.c includes it
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/defaults_override.h")
    message(STATUS "Using defaults_override.h for compile-time default values")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_DEFAULTS_OVERRIDE)
endif()
