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

static uint16_t map(uint16_t n, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max)
{
    uint16_t value = (n - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return value < out_min ? out_min : (value > out_max ? out_max : value);
}

static void touch_process_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    *x = map(*x, TOUCH_X_RES_MIN, TOUCH_X_RES_MAX, 0, TOUCH_X_DIM);
    *y = map(*y, TOUCH_Y_RES_MIN, TOUCH_Y_RES_MAX, 0, TOUCH_Y_DIM);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_point_data_t point_data = {0};
    uint8_t point_cnt = 0;

    esp_lcd_touch_get_data(s_tp, &point_data, &point_cnt, 1);

    data->state = LV_INDEV_STATE_RELEASED;

    if (point_cnt > 0) {
        data->point.x = point_data.x;
        data->point.y = point_data.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

esp_err_t touch_integration_init(esp_lcd_touch_handle_t *tp, int8_t spi_host_num, int8_t mosi_io_num, int8_t miso_io_num, int8_t sclk_io_num, int8_t cs_io_num, int8_t int_io_num)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;

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

    const esp_lcd_panel_io_spi_config_t touch_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(cs_io_num);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_host_num, &touch_io_cfg, &io_handle));

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

    esp_err_t ret = esp_lcd_touch_new_spi_xpt2046(io_handle, &touch_cfg, tp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize XPT2046: %s", esp_err_to_name(ret));
    } else {
        s_tp = *tp;
        ESP_LOGI(TAG, "XPT2046 initialized");
    }

    return ret;
}
