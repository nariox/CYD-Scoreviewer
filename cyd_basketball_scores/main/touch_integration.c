#include "touch_integration.h"
#include "lvgl.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "touch_int";

static esp_lcd_touch_handle_t s_tp = NULL;

static calibration_data_t s_calibration = {
    .x_min = 0,
    .x_max = 4095,
    .y_min = 0,
    .y_max = 4095,
    .swap_xy = true,
};

static touch_raw_adc_t s_last_raw = {0, 0};

void get_calibration_data(uint16_t *x_min, uint16_t *x_max, uint16_t *y_min, uint16_t *y_max)
{
    *x_min = s_calibration.x_min;
    *x_max = s_calibration.x_max;
    *y_min = s_calibration.y_min;
    *y_max = s_calibration.y_max;
}

static void touch_process_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    uint16_t cal_x_min = s_calibration.x_min;
    uint16_t cal_x_max = s_calibration.x_max;
    uint16_t cal_y_min = s_calibration.y_min;
    uint16_t cal_y_max = s_calibration.y_max;

    if (s_calibration.swap_xy) {
        uint16_t tmp = *x;
        *x = *y;
        *y = tmp;
    }

    // Get raw after swap to keep some sanity
    s_last_raw.x = *x;
    s_last_raw.y = *y;

    if (*x > cal_x_min) {
        *x = (uint16_t)((uint32_t)(*x - cal_x_min) * 319 / (cal_x_max - cal_x_min));
    } else {
        *x = 0;
    }

   if (*y > cal_y_min) {
        *y = (uint16_t)((uint32_t)(*y - cal_y_min) * 239 / (cal_y_max - cal_y_min));
    } else {
        *y = 0;
    }

    if (*x > 319) *x = 319;
    if (*y > 239) *y = 239;
}

void touch_integration_calibrate(touch_raw_adc_t *samples, calibration_data_t *out)
{
    out->x_min = samples[0].x;
    out->x_max = samples[0].x;
    out->y_min = samples[0].y;
    out->y_max = samples[0].y;

    for (int i = 1; i < 4; i++) {
        if (samples[i].x < out->x_min) out->x_min = samples[i].x;
        if (samples[i].x > out->x_max) out->x_max = samples[i].x;
        if (samples[i].y < out->y_min) out->y_min = samples[i].y;
        if (samples[i].y > out->y_max) out->y_max = samples[i].y;
    }
}

void touch_integration_apply_calibration(calibration_data_t *cal)
{
    s_calibration.x_min = cal->x_min;
    s_calibration.x_max = cal->x_max;
    s_calibration.y_min = cal->y_min;
    s_calibration.y_max = cal->y_max;
}

void touch_integration_get_raw_adc(uint16_t *x, uint16_t *y)
{
    if (x) *x = s_last_raw.x;
    if (y) *y = s_last_raw.y;
}

esp_err_t touch_integration_init(esp_lcd_touch_handle_t *tp)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;

    static const int SPI_MAX_TRANSFER_SIZE = 32768;
    const spi_bus_config_t buscfg_touch = {
        .mosi_io_num = PIN_TOUCH_MOSI,
        .miso_io_num = PIN_TOUCH_MISO,
        .sclk_io_num = PIN_TOUCH_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = SPI_MAX_TRANSFER_SIZE,
        .flags = SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MISO | SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TOUCH_HOST, &buscfg_touch, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t touch_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_TOUCH_CS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_HOST, &touch_io_cfg, &io_handle));

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = 4095,
        .y_max = 4095,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = PIN_TOUCH_IRQ,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .process_coordinates = touch_process_coordinates,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    };

    esp_err_t ret = esp_lcd_touch_new_spi_xpt2046(io_handle, &touch_cfg, tp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize XPT2046: %s", esp_err_to_name(ret));
    } else {
        s_tp = *tp;
        ESP_LOGI(TAG, "XPT2046 initialized");
    }

    return ret;
}
