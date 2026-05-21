#include "touch_integration.h"
#include "lvgl.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "touch_int";

static esp_lcd_touch_handle_t touch_handle = NULL;
static lv_indev_t *lv_indev = NULL;
static lv_display_t *lv_disp = NULL;

static uint16_t map(uint16_t n, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max)
{
    uint16_t value = (n - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return value < out_min ? out_min : (value > out_max ? out_max : value);
}

static void touch_process_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    ESP_LOGI(TAG, "pre-proc touch: raw_x=%u, raw_y=%u, strength=%u", x[0], y[0], strength[0]);
    *x = map(*x, TOUCH_X_RES_MIN, TOUCH_X_RES_MAX, 0, TOUCH_X_DIM);
    *y = map(*y, TOUCH_Y_RES_MIN, TOUCH_Y_RES_MAX, 0, TOUCH_Y_DIM);
    ESP_LOGI(TAG, "post-proc touch: raw_x=%u, raw_y=%u, strength=%u", x[0], y[0], strength[0]);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
//    ESP_LOGI(TAG, "read_cb called");

    esp_lcd_touch_read_data(touch_handle);

    data->state = LV_INDEV_STATE_RELEASED;

    uint16_t x[1], y[1], strength[1];
    uint8_t count = 0;

    if (esp_lcd_touch_get_coordinates(touch_handle, x, y, strength, &count, 1) && count > 0) {
        ESP_LOGI(TAG, "touch: count=%d, raw_x=%u, raw_y=%u, strength=%u", count, x[0], y[0], strength[0]);
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

esp_err_t touch_integration_init(lv_display_t *disp, int8_t spi_host_num, int8_t mosi_io_num, int8_t miso_io_num, int8_t sclk_io_num, int8_t cs_io_num, int8_t int_io_num)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;

    ESP_LOGI(TAG, "Initialize touch SPI3 bus");
    static const int SPI_MAX_TRANSFER_SIZE = 32768;
    const spi_bus_config_t buscfg_touch = {
        .mosi_io_num = mosi_io_num,
        .miso_io_num = miso_io_num,
        .sclk_io_num = sclk_io_num,
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
    ESP_ERROR_CHECK(spi_bus_initialize(spi_host_num, &buscfg_touch, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Initialize touch SPI panel IO");

    const esp_lcd_panel_io_spi_config_t touch_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(cs_io_num);

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_host_num, &touch_io_cfg, &io_handle));

    ESP_LOGI(TAG, "Initialize XPT2046 touch driver");

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = TOUCH_X_DIM,
        .y_max = TOUCH_Y_DIM,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = int_io_num,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = false,
        },
        .process_coordinates = touch_process_coordinates,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    };

    esp_err_t ret = esp_lcd_touch_new_spi_xpt2046(io_handle, &touch_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize XPT2046: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "XPT2046 initialized successfully");
    }

    ESP_LOGI(TAG, "Register LVGL input device");
    lv_disp = disp;
    lv_indev = lv_indev_create();
    lv_indev_set_type(lv_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_indev, touch_read_cb);
    lv_indev_set_display(lv_indev, disp);

    return ret;
}
