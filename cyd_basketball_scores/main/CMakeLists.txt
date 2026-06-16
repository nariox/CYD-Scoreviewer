idf_component_register(SRCS "app_main.c" "splash_screen.c" "cards_screen.c" "basketball_img.c" "touch_integration.c" "settings_screen.c" "wifi_screen.c" "touch_test_screen.c" "calibration_screen.c" "lcd.c" "nvs_settings.c" "wifi_backend.c"
                      INCLUDE_DIRS ".")

# If defaults_override.h exists, define CONFIG_DEFAULTS_OVERRIDE so nvs_settings.c includes it
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/defaults_override.h")
    message(STATUS "Using defaults_override.h for compile-time default values")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_DEFAULTS_OVERRIDE)
endif()
