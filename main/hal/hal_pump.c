/*
 * hal_pump.c
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#include "hal_pump.h"
#include "app_config.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "HAL_PUMP";

static bool i2c_initialized = false;

// Forward declaration
static esp_err_t pca9685_set_pwm_internal(uint8_t channel, uint16_t on, uint16_t off);
static esp_err_t pca9685_set_frequency_internal(uint16_t freq);

esp_err_t hal_pump_init(void) {
    if (i2c_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing pump via I2C...");

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = NPWT_I2C_SDA_GPIO,
        .scl_io_num = NPWT_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = NPWT_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_initialized = true;
    ESP_LOGI(TAG, "I2C master initialized on port %d", I2C_NUM_0);
    
    // Reset PCA9685
    uint8_t reset_cmd[] = {0x00, 0x00};
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR, reset_cmd, sizeof(reset_cmd), NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCA9685 reset failed, device may not be connected: %s", esp_err_to_name(ret));
        // Continue, to allow software-only operation
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Wait for reset
    
    ESP_LOGI(TAG, "Pump initialized successfully.");
    return hal_pump_set_frequency(1000); // Set a default frequency
}

esp_err_t hal_pump_deinit(void) {
    if (!i2c_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = i2c_driver_delete(I2C_NUM_0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete I2C driver: %s", esp_err_to_name(ret));
        return ret;
    }
    i2c_initialized = false;
    ESP_LOGI(TAG, "Pump deinitialized.");
    return ESP_OK;
}

esp_err_t hal_pump_set_duty(uint16_t duty) {
    if (duty > 4095) {
        return ESP_ERR_INVALID_ARG;
    }
    // PCA9685 duty is set by on/off time. For 0% duty, off=4096. For 100%, on=4096.
    // We can simplify and just set the off time.
    return pca9685_set_pwm_internal(NPWT_PCA9685_PWM_CHANNEL, 0, duty);
}

esp_err_t hal_pump_set_frequency(uint16_t frequency_hz) {
    return pca9685_set_frequency_internal(frequency_hz);
}

static esp_err_t pca9685_set_pwm_internal(uint8_t channel, uint16_t on, uint16_t off) {
    if (!i2c_initialized) {
        ESP_LOGW(TAG, "I2C not initialized. Simulating PWM set.");
        return ESP_OK; // Allow to run without hardware
    }
    uint8_t data[5];
    data[0] = 0x06 + 4 * channel; // Register base for channel
    data[1] = on & 0xFF;
    data[2] = on >> 8;
    data[3] = off & 0xFF;
    data[4] = off >> 8;

    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR, data, sizeof(data), NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set PWM on channel %d: %s", channel, esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Set PWM channel %d to on=%d, off=%d", channel, on, off);
    }
    return ret;
}

static esp_err_t pca9685_set_frequency_internal(uint16_t freq) {
    if (!i2c_initialized) {
        ESP_LOGW(TAG, "I2C not initialized. Simulating frequency set.");
        return ESP_OK;
    }
    
    uint8_t prescale = (uint8_t)(round(25000000.0f / (4096.0f * freq)) - 1);
    
    uint8_t oldmode;
    uint8_t reg_addr = 0x00; // MODE1
    esp_err_t ret = i2c_master_write_read_device(I2C_NUM_0, NPWT_PCA9685_ADDR, &reg_addr, 1, &oldmode, 1, NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read MODE1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    uint8_t newmode = (oldmode & 0x7F) | 0x10; // sleep
    uint8_t write_buf[2] = {reg_addr, newmode};
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR, write_buf, 2, NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) return ret;

    write_buf[0] = 0xFE; // PRESCALE
    write_buf[1] = prescale;
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR, write_buf, 2, NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) return ret;

    write_buf[0] = reg_addr; // MODE1
    write_buf[1] = oldmode;
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR, write_buf, 2, NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(5));

    write_buf[1] = oldmode | 0xa0; // auto-increment
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR, write_buf, 2, NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Set PWM frequency to approx %d Hz", freq);
    return ret;
}

