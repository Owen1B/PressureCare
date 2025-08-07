/*
 * hal_pressure_sensor.c
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#include "hal_pressure_sensor.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "HAL_PRESSURE";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_calibrated = false;

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void adc_calibration_deinit(adc_cali_handle_t handle);

esp_err_t hal_pressure_sensor_init(void) {
    ESP_LOGI(TAG, "Initializing pressure sensor...");

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = NPWT_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = NPWT_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(adc_handle, NPWT_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }

    adc_calibrated = adc_calibration_init(NPWT_ADC_UNIT, NPWT_ADC_CHANNEL, NPWT_ADC_ATTEN, &adc_cali_handle);
    if (adc_calibrated) {
        ESP_LOGI(TAG, "ADC calibration successful.");
    } else {
        ESP_LOGW(TAG, "ADC calibration failed, using raw values.");
    }

    ESP_LOGI(TAG, "Pressure sensor initialized successfully.");
    return ESP_OK;
}

esp_err_t hal_pressure_sensor_deinit(void) {
    if (adc_calibrated && adc_cali_handle) {
        adc_calibration_deinit(adc_cali_handle);
        adc_cali_handle = NULL;
        adc_calibrated = false;
    }

    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }
    ESP_LOGI(TAG, "Pressure sensor deinitialized.");
    return ESP_OK;
}

esp_err_t hal_pressure_sensor_read(int16_t *pressure_kpa) {
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "Sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int adc_raw = 0;
    int voltage_mv = 0;

    esp_err_t ret = adc_oneshot_read(adc_handle, NPWT_ADC_CHANNEL, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    if (adc_calibrated) {
        ret = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to calibrate voltage: %s", esp_err_to_name(ret));
            // Fallback to raw calculation
            voltage_mv = (adc_raw * 3300) / 4095;
        }
    } else {
        voltage_mv = (adc_raw * 3300) / 4095;
    }

    float relative_pressure = (voltage_mv - NPWT_ZERO_POINT_VOLTAGE) * NPWT_PRESSURE_COEFFICIENT;
    *pressure_kpa = (int16_t)(relative_pressure + (relative_pressure >= 0 ? 0.5f : -0.5f));

    ESP_LOGD(TAG, "Raw: %d, Voltage: %d mV, Pressure: %d kPa", adc_raw, voltage_mv, *pressure_kpa);

    return ESP_OK;
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    ESP_LOGI(TAG, "Attempting ADC calibration with Curve Fitting scheme...");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Curve fitting calibration not supported, eFuse bits may not be burnt.");
    } else {
        ESP_LOGE(TAG, "Failed to create calibration scheme: %s", esp_err_to_name(ret));
    }

    *out_handle = handle;
    return calibrated;
}

static void adc_calibration_deinit(adc_cali_handle_t handle) {
    if (handle) {
        ESP_LOGI(TAG, "Deregistering ADC calibration scheme...");
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
    }
}

