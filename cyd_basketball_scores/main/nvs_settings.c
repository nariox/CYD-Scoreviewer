#include "nvs_settings.h"

#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "nvs_settings";

static esp_err_t nvs_settings_open_ro(nvs_handle_t *handle)
{
    return nvs_open(NVS_SETTINGS_NS, NVS_READONLY, handle);
}

static esp_err_t nvs_settings_open_rw(nvs_handle_t *handle)
{
    return nvs_open(NVS_SETTINGS_NS, NVS_READWRITE, handle);
}

esp_err_t nvs_settings_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs upgrade, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
}

esp_err_t nvs_settings_save_brightness(uint8_t brightness)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing brightness");
        return ret;
    }
    ret = nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, brightness);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Brightness saved: %u", brightness);
    }
    return ret;
}

esp_err_t nvs_settings_save_calibration(const calibration_data_t *cal)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing calibration");
        return ret;
    }
    ret = nvs_set_blob(handle, NVS_KEY_CALIBRATION, cal, sizeof(calibration_data_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Calibration saved: x_min=%u x_max=%u y_min=%u y_max=%u swap_xy=%u",
                 cal->x_min, cal->x_max, cal->y_min, cal->y_max, cal->swap_xy);
    }
    return ret;
}

esp_err_t nvs_settings_save_calibration_data(bool saved)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing calibration_saved");
        return ret;
    }
    ret = nvs_set_u8(handle, NVS_KEY_CALIBRATION_SAVED, saved ? 1 : 0);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Calibration saved flag: %s", saved ? "true" : "false");
    }
    return ret;
}

esp_err_t nvs_settings_save_last_screen(uint8_t screen_id)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing last_screen");
        return ret;
    }
    ret = nvs_set_u8(handle, NVS_KEY_LAST_SCREEN, screen_id);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Last screen saved: %u", screen_id);
    }
    return ret;
}

esp_err_t nvs_settings_load_brightness(uint8_t *brightness)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_ro(&handle);
    if (ret != ESP_OK) {
        return ret;
    }
    uint8_t val;
    ret = nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &val);
    nvs_close(handle);
    if (ret == ESP_OK) {
        *brightness = val;
        ESP_LOGD(TAG, "Brightness loaded: %u", val);
    } else {
        ESP_LOGD(TAG, "No brightness setting in NVS, using default");
    }
    return ret;
}

esp_err_t nvs_settings_load_calibration(calibration_data_t *cal)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_ro(&handle);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t len = sizeof(calibration_data_t);
    ret = nvs_get_blob(handle, NVS_KEY_CALIBRATION, cal, &len);
    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Calibration loaded: x_min=%u x_max=%u y_min=%u y_max=%u swap_xy=%u",
                 cal->x_min, cal->x_max, cal->y_min, cal->y_max, cal->swap_xy);
    } else {
        ESP_LOGD(TAG, "No calibration in NVS, using defaults");
    }
    return ret;
}

esp_err_t nvs_settings_load_last_screen(uint8_t *screen_id)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_ro(&handle);
    if (ret != ESP_OK) {
        return ret;
    }
    uint8_t val;
    ret = nvs_get_u8(handle, NVS_KEY_LAST_SCREEN, &val);
    nvs_close(handle);
    if (ret == ESP_OK) {
        *screen_id = val;
        ESP_LOGD(TAG, "Last screen loaded: %u", val);
    } else {
        ESP_LOGD(TAG, "No last_screen in NVS, using default");
    }
    return ret;
}

esp_err_t nvs_settings_load_all(nvs_settings_t *settings)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_ro(&handle);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Load brightness */
    ret = nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &settings->brightness);
    if (ret != ESP_OK) {
        settings->brightness = 128; /* default */
    }

    /* Load calibration */
    size_t cal_len = sizeof(calibration_data_t);
    ret = nvs_get_blob(handle, NVS_KEY_CALIBRATION, &settings->calibration, &cal_len);
    if (ret != ESP_OK) {
        /* Use defaults */
        settings->calibration.x_min = 0;
        settings->calibration.x_max = 4095;
        settings->calibration.y_min = 0;
        settings->calibration.y_max = 4095;
        settings->calibration.swap_xy = true;
    }

    /* Load calibration_saved flag */
    uint8_t cal_saved_u8;
    ret = nvs_get_u8(handle, NVS_KEY_CALIBRATION_SAVED, &cal_saved_u8);
    if (ret == ESP_OK) {
        settings->calibration_saved = (cal_saved_u8 != 0);
    } else {
        settings->calibration_saved = false;
    }

    /* Load last screen */
    ret = nvs_get_u8(handle, NVS_KEY_LAST_SCREEN, &settings->last_screen);
    if (ret != ESP_OK) {
        settings->last_screen = LAST_SCREEN_SPLASH; /* default */
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_settings_reset(void)
{
    return nvs_flash_erase();
}
