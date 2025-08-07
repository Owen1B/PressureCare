/*
 * npwt_core.c
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 *
 *  Description: This file acts as the central point for system state management
 *               and provides an interface for the UI to interact with the core logic.
 */

#include "npwt_core.h"
#include "npwt_logic.h" // Include the new logic layer
#include "app_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "NPWT_CORE";

// The single global instance of the system state, shared across modules
npwt_system_t g_npwt_system = {0};

// Private function prototypes for NVS handling
static void load_settings_from_nvs(void);
static void set_default_settings(void);
static esp_err_t save_settings_to_nvs(const npwt_settings_t *settings);

// --- System Lifecycle Functions ---

esp_err_t npwt_system_init(void) {
    ESP_LOGI(TAG, "Initializing NPWT system core...");
    
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Load settings from NVS or use defaults
    load_settings_from_nvs();

    // 3. Initialize the core logic module
    return npwt_logic_init();
}

esp_err_t npwt_system_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing NPWT system core...");
    return npwt_logic_deinit();
}

esp_err_t npwt_system_start(void) {
    ESP_LOGI(TAG, "Starting NPWT system core...");
    npwt_set_power(true);
    return npwt_logic_start();
}

esp_err_t npwt_system_stop(void) {
    ESP_LOGI(TAG, "Stopping NPWT system core...");
    npwt_set_power(false);
    return npwt_logic_stop();
}

// --- Data Access and Control Functions (Interface for UI) ---

void npwt_set_target_pressure(int16_t pressure) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.target_pressure = pressure;
    xSemaphoreGive(g_npwt_system.data_mutex);
}

void npwt_set_work_time(uint8_t minutes) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.work_time = minutes;
    xSemaphoreGive(g_npwt_system.data_mutex);
}

void npwt_set_rest_time(uint8_t minutes) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.rest_time = minutes;
    xSemaphoreGive(g_npwt_system.data_mutex);
}

void npwt_set_mode(npwt_mode_t mode) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    if (g_npwt_system.settings.mode != mode) {
        g_npwt_system.settings.mode = mode;
        g_npwt_system.realtime.state = NPWT_STATE_IDLE;
        g_npwt_system.realtime.work_elapsed = 0;
        g_npwt_system.realtime.rest_elapsed = 0;
    }
    xSemaphoreGive(g_npwt_system.data_mutex);
}

void npwt_set_power(bool power_on) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.power_on = power_on;
    if (!power_on) {
         g_npwt_system.realtime.state = NPWT_STATE_IDLE;
    }
    xSemaphoreGive(g_npwt_system.data_mutex);
}

npwt_settings_t npwt_get_settings(void) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    npwt_settings_t settings = g_npwt_system.settings;
    xSemaphoreGive(g_npwt_system.data_mutex);
    return settings;
}

npwt_realtime_t npwt_get_realtime_data(void) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    npwt_realtime_t realtime = g_npwt_system.realtime;
    xSemaphoreGive(g_npwt_system.data_mutex);
    return realtime;
}

void npwt_register_ui_callback(void (*callback)(void)) {
    g_npwt_system.ui_update_callback = callback;
}

esp_err_t npwt_settings_save(const npwt_settings_t *settings) {
    return save_settings_to_nvs(settings);
}

// --- NVS Handling ---

static void load_settings_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (%s), using default settings.", esp_err_to_name(err));
        set_default_settings();
        return;
    }

    int16_t pressure;
    uint8_t work_time, rest_time, mode;

    bool success = true;
    if(nvs_get_i16(nvs_handle, NVS_KEY_PRESSURE, &pressure) != ESP_OK) success = false;
    if(nvs_get_u8(nvs_handle, NVS_KEY_WORK_TIME, &work_time) != ESP_OK) success = false;
    if(nvs_get_u8(nvs_handle, NVS_KEY_REST_TIME, &rest_time) != ESP_OK) success = false;
    if(nvs_get_u8(nvs_handle, NVS_KEY_MODE, &mode) != ESP_OK) success = false;

    nvs_close(nvs_handle);

    if (success) {
        g_npwt_system.settings.target_pressure = pressure;
        g_npwt_system.settings.work_time = work_time;
        g_npwt_system.settings.rest_time = rest_time;
        g_npwt_system.settings.mode = (npwt_mode_t)mode;
        ESP_LOGI(TAG, "Settings loaded from NVS.");
    } else {
        ESP_LOGW(TAG, "Failed to load one or more settings from NVS, using defaults.");
        set_default_settings();
    }
    
    g_npwt_system.settings.power_on = false; // Always start powered off
}

static void set_default_settings(void) {
    ESP_LOGI(TAG, "Resetting settings to default values.");
    g_npwt_system.settings.target_pressure = NPWT_PRESSURE_DEFAULT;
    g_npwt_system.settings.work_time = NPWT_TIME_WORK_DEFAULT;
    g_npwt_system.settings.rest_time = NPWT_TIME_REST_DEFAULT;
    g_npwt_system.settings.mode = NPWT_MODE_CONTINUOUS;
    g_npwt_system.settings.power_on = false;
}

static esp_err_t save_settings_to_nvs(const npwt_settings_t *settings) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    esp_err_t write_err = ESP_OK;
    if(nvs_set_i16(nvs_handle, NVS_KEY_PRESSURE, settings->target_pressure) != ESP_OK) write_err = ESP_FAIL;
    if(nvs_set_u8(nvs_handle, NVS_KEY_WORK_TIME, settings->work_time) != ESP_OK) write_err = ESP_FAIL;
    if(nvs_set_u8(nvs_handle, NVS_KEY_REST_TIME, settings->rest_time) != ESP_OK) write_err = ESP_FAIL;
    if(nvs_set_u8(nvs_handle, NVS_KEY_MODE, (uint8_t)settings->mode) != ESP_OK) write_err = ESP_FAIL;
    
    if (write_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write one or more settings to NVS.");
        nvs_close(nvs_handle);
        return ESP_FAIL;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Settings saved successfully to NVS.");
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
    }

    return err;
}
