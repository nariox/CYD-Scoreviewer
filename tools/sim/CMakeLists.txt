cmake_minimum_required(VERSION 3.16)
project(cyd_sim C)

set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cyd_basketball_scores/managed_components/lvgl__lvgl)
set(MAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cyd_basketball_scores/main)

file(GLOB_RECURSE LVGL_SRCS ${LVGL_DIR}/src/*.c)

add_executable(cyd_sim
    main.c
    stubs.c
    ${MAIN_DIR}/cards_screen.c
    ${MAIN_DIR}/wifi_glyph_img.c
    ${MAIN_DIR}/game_detail_screen.c
    ${MAIN_DIR}/league_logos.c
    ${MAIN_DIR}/espn_api.c
    ${MAIN_DIR}/team_logos.c
    ${LVGL_SRCS}
)

target_include_directories(cyd_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # lv_conf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/shim     # esp_err.h / esp_log.h / ...
    ${LVGL_DIR}
    ${MAIN_DIR}
)

target_compile_definitions(cyd_sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
set_target_properties(cyd_sim PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_compile_options(cyd_sim PRIVATE -O1 -w)
