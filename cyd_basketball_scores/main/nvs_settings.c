#include "nvs_settings.h"

#include "nvs_flash.h"
#include "esp_log.h"

#ifdef CONFIG_DEFAULTS_OVERRIDE
#include "defaults_override.h"
#endif

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
    ESP_LOGI(TAG, "Saving calibration: x_min=%u x_max=%u y_min=%u y_max=%u swap_xy=%u",
             cal->x_min, cal->x_max, cal->y_min, cal->y_max, cal->swap_xy);

    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing calibration: %d", ret);
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_CALIBRATION, cal, sizeof(calibration_data_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed for calibration: %d", ret);
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed for calibration: %d", ret);
        nvs_close(handle);
        return ret;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Calibration saved successfully");
    return ESP_OK;
}

esp_err_t nvs_settings_save_calibration_data(bool saved)
{
    ESP_LOGI(TAG, "Saving calibration_saved flag: %s", saved ? "true" : "false");

    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing calibration_saved: %d", ret);
        return ret;
    }

    ret = nvs_set_u8(handle, NVS_KEY_CALIBRATION_SAVED, saved ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u8 failed for calibration_saved: %d", ret);
        nvs_close(handle);
        return ret;
    }

    /* Write schema version so future firmware version mismatches are detected */
    if (ret == ESP_OK) {
        ret = nvs_set_u32(handle, NVS_KEY_SCHEMA_VER, NVS_SCHEMA_VERSION);
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved flag + schema version set successfully");
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

static bool validate_calibration(const calibration_data_t *cal)
{
    /* x_min and y_min must be < 2048 (lower half of ADC range) */
    /* x_max and y_max must be >= 2048 (upper half of ADC range) */
    if (cal->x_min >= 2048) {
        ESP_LOGW(TAG, "Validation failed: x_min=%u >= 2048", cal->x_min);
        return false;
    }
    if (cal->y_min >= 2048) {
        ESP_LOGW(TAG, "Validation failed: y_min=%u >= 2048", cal->y_min);
        return false;
    }
    if (cal->x_max < 2048) {
        ESP_LOGW(TAG, "Validation failed: x_max=%u < 2048", cal->x_max);
        return false;
    }
    if (cal->y_max < 2048) {
        ESP_LOGW(TAG, "Validation failed: y_max=%u < 2048", cal->y_max);
        return false;
    }
    return true;
}

static void apply_defaults(nvs_settings_t *settings)
{
    settings->brightness = NVS_DEFAULT_BRIGHTNESS;
    settings->calibration.x_min = NVS_DEFAULT_CAL_X_MIN;
    settings->calibration.x_max = NVS_DEFAULT_CAL_X_MAX;
    settings->calibration.y_min = NVS_DEFAULT_CAL_Y_MIN;
    settings->calibration.y_max = NVS_DEFAULT_CAL_Y_MAX;
    settings->calibration.swap_xy = NVS_DEFAULT_CAL_SWAP_XY;
    settings->calibration_saved = false;
    settings->last_screen = LAST_SCREEN_SPLASH;
}

esp_err_t nvs_settings_load_all(nvs_settings_t *settings)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_ro(&handle);
    if (ret != ESP_OK) {
        apply_defaults(settings);
        return ret;
    }

    /* Check schema version first */
    uint32_t schema_ver = 0;
    ret = nvs_get_u32(handle, NVS_KEY_SCHEMA_VER, &schema_ver);
    if (ret != ESP_OK || schema_ver != NVS_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "Schema version mismatch (nvs=%u, firmware=%u), using defaults",
                 schema_ver, NVS_SCHEMA_VERSION);
        nvs_close(handle);
        apply_defaults(settings);
        return ESP_OK;
    }

    /* Load brightness */
    ret = nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &settings->brightness);
    if (ret != ESP_OK) {
        settings->brightness = NVS_DEFAULT_BRIGHTNESS;
    }

    /* Load calibration */
    size_t cal_len = sizeof(calibration_data_t);
    ret = nvs_get_blob(handle, NVS_KEY_CALIBRATION, &settings->calibration, &cal_len);
    if (ret != ESP_OK) {
        settings->calibration.x_min = NVS_DEFAULT_CAL_X_MIN;
        settings->calibration.x_max = NVS_DEFAULT_CAL_X_MAX;
        settings->calibration.y_min = NVS_DEFAULT_CAL_Y_MIN;
        settings->calibration.y_max = NVS_DEFAULT_CAL_Y_MAX;
        settings->calibration.swap_xy = NVS_DEFAULT_CAL_SWAP_XY;
    }

    /* Validate calibration quadrant consistency */
    bool cal_valid = validate_calibration(&settings->calibration);

    /* Load calibration_saved flag */
    uint8_t cal_saved_u8;
    ret = nvs_get_u8(handle, NVS_KEY_CALIBRATION_SAVED, &cal_saved_u8);
    if (ret == ESP_OK && cal_valid) {
        settings->calibration_saved = (cal_saved_u8 != 0);
        ESP_LOGI(TAG, "cal_saved loaded: %s (raw=%u)", settings->calibration_saved ? "true" : "false", cal_saved_u8);
    } else {
        settings->calibration_saved = false;
        if (!cal_valid) {
            ESP_LOGW(TAG, "Calibration INVALID (x_min=%u x_max=%u y_min=%u y_max=%u), using defaults",
                     settings->calibration.x_min, settings->calibration.x_max,
                     settings->calibration.y_min, settings->calibration.y_max);
            settings->calibration.x_min = NVS_DEFAULT_CAL_X_MIN;
            settings->calibration.x_max = NVS_DEFAULT_CAL_X_MAX;
            settings->calibration.y_min = NVS_DEFAULT_CAL_Y_MIN;
            settings->calibration.y_max = NVS_DEFAULT_CAL_Y_MAX;
            settings->calibration.swap_xy = NVS_DEFAULT_CAL_SWAP_XY;
        } else {
            ESP_LOGI(TAG, "cal_saved not found in NVS (err=%d), defaulting to false", ret);
        }
    }

    /* Load last screen */
    ret = nvs_get_u8(handle, NVS_KEY_LAST_SCREEN, &settings->last_screen);
    if (ret != ESP_OK) {
        settings->last_screen = LAST_SCREEN_SPLASH;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_settings_reset(void)
{
    return nvs_flash_erase();
}

static const char *const s_wifi_ssid_keys[NVS_MAX_WIFI_NETWORKS] = {
    NVS_KEY_WIFI_SSID_0, NVS_KEY_WIFI_SSID_1, NVS_KEY_WIFI_SSID_2
};

static const char *const s_wifi_pwd_keys[NVS_MAX_WIFI_NETWORKS] = {
    NVS_KEY_WIFI_PWD_0, NVS_KEY_WIFI_PWD_1, NVS_KEY_WIFI_PWD_2
};

esp_err_t nvs_settings_save_wifi_network(uint8_t index, const char *ssid, const char *password)
{
    if (index >= NVS_MAX_WIFI_NETWORKS) {
        ESP_LOGE(TAG, "WiFi network index out of range: %u", index);
        return ESP_ERR_INVALID_ARG;
    }
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing WiFi network %u", index);
        return ret;
    }

    ret = nvs_set_str(handle, s_wifi_ssid_keys[index], ssid);
    if (ret != ESP_OK) goto exit;

    if (password && strlen(password) > 0) {
        ret = nvs_set_str(handle, s_wifi_pwd_keys[index], password);
        if (ret != ESP_OK) goto exit;
    } else {
        ret = nvs_set_str(handle, s_wifi_pwd_keys[index], "");
        if (ret != ESP_OK) goto exit;
    }

    /* Update count */
    uint8_t count;
    ret = nvs_get_u8(handle, NVS_KEY_WIFI_COUNT, &count);
    if (ret != ESP_OK) count = 0;
    if (index >= count) {
        count = index + 1;
    }
    ret = nvs_set_u8(handle, NVS_KEY_WIFI_COUNT, count);
    if (ret != ESP_OK) goto exit;

    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi network %u saved: %s", index, ssid);
    }

exit:
    nvs_close(handle);
    return ret;
}

esp_err_t nvs_settings_load_wifi_networks(nvs_wifi_credentials_t *creds)
{
    if (!creds) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(creds, 0, sizeof(*creds));

    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_ro(&handle);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t count;
    ret = nvs_get_u8(handle, NVS_KEY_WIFI_COUNT, &count);
    if (ret != ESP_OK) {
        count = 0;
    }
    if (count > NVS_MAX_WIFI_NETWORKS) {
        count = NVS_MAX_WIFI_NETWORKS;
    }
    creds->count = count;

    for (uint8_t i = 0; i < count; i++) {
        size_t len = NVS_MAX_SSID_LEN;
        ret = nvs_get_str(handle, s_wifi_ssid_keys[i], creds->networks[i].ssid, &len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load WiFi network %u SSID", i);
            continue;
        }

        len = NVS_MAX_PWD_LEN;
        ret = nvs_get_str(handle, s_wifi_pwd_keys[i], creds->networks[i].password, &len);
        if (ret != ESP_OK) {
            creds->networks[i].password[0] = '\0';
        }

        ESP_LOGD(TAG, "Loaded WiFi network %u: %s", i, creds->networks[i].ssid);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded %u WiFi network(s)", creds->count);
    return ESP_OK;
}

esp_err_t nvs_settings_delete_wifi_network(uint8_t index)
{
    if (index >= NVS_MAX_WIFI_NETWORKS) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_erase_key(handle, s_wifi_ssid_keys[index]);
    nvs_erase_key(handle, s_wifi_pwd_keys[index]);

    /* Update count */
    uint8_t count;
    ret = nvs_get_u8(handle, NVS_KEY_WIFI_COUNT, &count);
    if (ret == ESP_OK && index < count) {
        count--;
        ret = nvs_set_u8(handle, NVS_KEY_WIFI_COUNT, count);
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WiFi network %u deleted", index);
        }
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_settings_clear_all_wifi_networks(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_settings_open_rw(&handle);
    if (ret != ESP_OK) {
        return ret;
    }

    for (uint8_t i = 0; i < NVS_MAX_WIFI_NETWORKS; i++) {
        nvs_erase_key(handle, s_wifi_ssid_keys[i]);
        nvs_erase_key(handle, s_wifi_pwd_keys[i]);
    }
    nvs_erase_key(handle, NVS_KEY_WIFI_COUNT);

    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "All WiFi networks cleared");
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_settings_find_wifi_network(const char *ssid, uint8_t *index)
{
    if (!ssid || !index) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_wifi_credentials_t creds;
    esp_err_t ret = nvs_settings_load_wifi_networks(&creds);
    if (ret != ESP_OK) {
        return ret;
    }

    for (uint8_t i = 0; i < creds.count; i++) {
        if (strcmp(creds.networks[i].ssid, ssid) == 0) {
            *index = i;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}
