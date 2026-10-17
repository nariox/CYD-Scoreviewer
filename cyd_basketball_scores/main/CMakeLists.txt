idf_component_register(SRCS "app_main.c" "splash_screen.c" "cards_screen.c" "game_detail_screen.c" "team_logos.c" "basketball_img.c" "touch_integration.c" "settings_screen.c" "wifi_screen.c" "touch_test_screen.c" "calibration_screen.c" "lcd.c" "nvs_settings.c" "wifi_backend.c" "nba_api.c" "nba_service.c" "time_sync.c"
                      INCLUDE_DIRS ".")

# Optional short refresh interval for soak-testing the score feed worker:
#   idf.py -DCYD_NBA_REFRESH_S=60 build      (short interval)
#   idf.py -DCYD_NBA_REFRESH_S= build        (back to the default)
if(NOT CYD_NBA_REFRESH_S STREQUAL "")
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

# If defaults_override.h exists, define CONFIG_DEFAULTS_OVERRIDE so nvs_settings.c includes it
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/defaults_override.h")
    message(STATUS "Using defaults_override.h for compile-time default values")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_DEFAULTS_OVERRIDE)
endif()
