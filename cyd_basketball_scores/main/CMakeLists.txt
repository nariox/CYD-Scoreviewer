idf_component_register(SRCS "app_main.c" "splash_screen.c" "cards_screen.c" "game_detail_screen.c" "team_logos.c" "league_logos.c" "basketball_img.c" "touch_integration.c" "settings_screen.c" "wifi_screen.c" "touch_test_screen.c" "calibration_screen.c" "lcd.c" "nvs_settings.c" "wifi_backend.c" "nba_api.c" "espn_api.c" "nba_service.c" "time_sync.c"
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

# If defaults_override.h exists, define CONFIG_DEFAULTS_OVERRIDE so nvs_settings.c includes it
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/defaults_override.h")
    message(STATUS "Using defaults_override.h for compile-time default values")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_DEFAULTS_OVERRIDE)
endif()
