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
static lv_obj_t *coords_label = NULL;

static void touch_process_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    uint16_t tmp = *x;
    *x = *y;
    *y = tmp;
    *x = LV_HOR_RES - 1 - *x;
    *y = LV_VER_RES - 1 - *y;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x[1];
    uint16_t y[1];
    uint16_t strength[1];
    uint8_t count = 0;

    esp_lcd_touch_read_data(touch_handle);

    data->state = LV_INDEV_STATE_RELEASED;

    if (esp_lcd_touch_get_coordinates(touch_handle, x, y, strength, &count, 1)) {
        if (x[0] >= LV_HOR_RES || y[0] >= LV_VER_RES) {
            data->continue_reading = 0;
            return;
        }
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
        data->continue_reading = 1;

        ESP_LOGI(TAG, "Touch: x=%u, y=%u", x[0], y[0]);

        if (coords_label) {
            char buf[32];
            snprintf(buf, sizeof(buf), "X: %u  Y: %u", x[0], y[0]);
            lv_label_set_text(coords_label, buf);
        }
    } else {
        data->continue_reading = 0;
        if (coords_label) {
            lv_label_set_text(coords_label, "X: --- Y: ---");
        }
    }
}

static void touch_poll_task(void *arg)
{
    uint16_t last_x = 0xFFFF, last_y = 0xFFFF;

    while (1) {
        if (!touch_handle) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint16_t x[1];
        uint16_t y[1];
        uint16_t strength[1];
        uint8_t count = 0;

        esp_lcd_touch_read_data(touch_handle);

        if (esp_lcd_touch_get_coordinates(touch_handle, x, y, strength, &count, 1)) {
            if (x[0] != last_x || y[0] != last_y) {
                ESP_LOGI(TAG, "Poll touch: x=%u, y=%u", x[0], y[0]);
                last_x = x[0];
                last_y = y[0];
            }
        } else {
            last_x = 0xFFFF;
            last_y = 0xFFFF;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void touch_integration_set_coords_label(lv_obj_t *label)
{
    coords_label = label;
}

esp_err_t touch_integration_init(int8_t spi_host_num, int8_t mosi_io_num, int8_t miso_io_num, int8_t sclk_io_num, int8_t cs_io_num, int8_t int_io_num)
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
        .x_max = 240,
        .y_max = 320,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = int_io_num,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = true,
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
    lv_indev = lv_indev_create();
    lv_indev_set_type(lv_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_indev, touch_read_cb);
    lv_indev_set_display(lv_indev, NULL);

    ESP_LOGI(TAG, "Starting touch poll task (50ms)");
    xTaskCreate(touch_poll_task, "touch_poll", 4096, NULL, 5, NULL);

    return ret;
}
