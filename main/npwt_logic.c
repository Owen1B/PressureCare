/*
 * npwt_logic.c
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#include "npwt_logic.h"
#include "npwt_core.h"
#include "app_config.h"
#include "hal/hal_pressure_sensor.h"
#include "hal/hal_pump.h"
#include "algorithms/pid_controller.h"
#include "algorithms/kalman_filter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "NPWT_LOGIC";

// Global system instance from npwt_core.c
extern npwt_system_t g_npwt_system;

static kalman_filter_t pressure_kalman;
static pid_controller_t pressure_pid;
static TaskHandle_t control_task_handle = NULL;
static TimerHandle_t mode_timer_handle = NULL;

static void control_task(void *pvParameters);
static void mode_timer_callback(TimerHandle_t xTimer);
static void run_continuous_mode(void);
static void run_intermittent_mode(void);

esp_err_t npwt_logic_init(void) {
    ESP_LOGI(TAG, "Initializing NPWT logic...");

    ESP_ERROR_CHECK(hal_pressure_sensor_init());
    ESP_ERROR_CHECK(hal_pump_init());

    ESP_ERROR_CHECK(kalman_filter_init(&pressure_kalman, NPWT_KALMAN_Q, NPWT_KALMAN_R, 0.0f));
    
    // Note: The original PID output was inverted. Here we use a direct mapping.
    // The pump is off at duty=4095 (100%) and max speed at duty=0 (0%).
    // So, PID output needs to be mapped to this inverted range.
    // We will handle this inversion in the control logic.
    ESP_ERROR_CHECK(pid_controller_init(&pressure_pid, NPWT_PID_KP, NPWT_PID_KI, NPWT_PID_KD, 0, 4095));
    
    g_npwt_system.data_mutex = xSemaphoreCreateMutex();
    if (!g_npwt_system.data_mutex) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        return ESP_FAIL;
    }

    mode_timer_handle = xTimerCreate("mode_timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, mode_timer_callback);

    ESP_LOGI(TAG, "NPWT logic initialized.");
    return ESP_OK;
}

esp_err_t npwt_logic_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing NPWT logic...");
    npwt_logic_stop();
    
    if (g_npwt_system.data_mutex) {
        vSemaphoreDelete(g_npwt_system.data_mutex);
        g_npwt_system.data_mutex = NULL;
    }

    hal_pressure_sensor_deinit();
    hal_pump_deinit();
    
    ESP_LOGI(TAG, "NPWT logic deinitialized.");
    return ESP_OK;
}

esp_err_t npwt_logic_start(void) {
    ESP_LOGI(TAG, "Starting NPWT logic task...");
    BaseType_t ret = xTaskCreate(control_task, "npwt_control", 4096, NULL, 5, &control_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control task");
        return ESP_FAIL;
    }
    xTimerStart(mode_timer_handle, 0);
    return ESP_OK;
}

esp_err_t npwt_logic_stop(void) {
    ESP_LOGI(TAG, "Stopping NPWT logic task...");
    if (control_task_handle) {
        vTaskDelete(control_task_handle);
        control_task_handle = NULL;
    }
    if (mode_timer_handle) {
        xTimerStop(mode_timer_handle, 0);
    }
    hal_pump_set_duty(NPWT_PWM_MAX); // Ensure pump is off
    return ESP_OK;
}

static void control_task(void *pvParameters) {
    while (1) {
        xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
        
        // 1. Read Sensor Data
        int16_t raw_pressure;
        hal_pressure_sensor_read(&raw_pressure);
        
        // 2. Filter Data
        g_npwt_system.realtime.current_pressure = kalman_filter_update(&pressure_kalman, (float)raw_pressure);

        if (g_npwt_system.settings.power_on) {
            // 3. Run State Machine / Control Logic
            switch (g_npwt_system.settings.mode) {
                case NPWT_MODE_CONTINUOUS:
                    run_continuous_mode();
                    break;
                case NPWT_MODE_INTERMITTENT:
                    run_intermittent_mode();
                    break;
            }
        } else {
            // System is off, ensure pump is stopped
            hal_pump_set_duty(NPWT_PWM_MAX);
            g_npwt_system.realtime.pump_pwm = NPWT_PWM_MAX;
            g_npwt_system.realtime.state = NPWT_STATE_IDLE;
        }

        xSemaphoreGive(g_npwt_system.data_mutex);
        
        // 4. Update UI (if callback is registered)
        if (g_npwt_system.ui_update_callback) {
            g_npwt_system.ui_update_callback();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Control loop at 10Hz
    }
}

static void mode_timer_callback(TimerHandle_t xTimer) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    if (g_npwt_system.settings.power_on) {
        if (g_npwt_system.realtime.state == NPWT_STATE_WORKING) {
            g_npwt_system.realtime.work_elapsed++;
        } else if (g_npwt_system.realtime.state == NPWT_STATE_RESTING) {
            g_npwt_system.realtime.rest_elapsed++;
        }
    }
    xSemaphoreGive(g_npwt_system.data_mutex);
}

static void run_continuous_mode() {
    g_npwt_system.realtime.state = NPWT_STATE_WORKING;
    
    float target_pressure = (float)g_npwt_system.settings.target_pressure;
    float current_pressure = (float)g_npwt_system.realtime.current_pressure;

    // The PID is configured to output a value where higher means more pump action.
    // However, pump is controlled by PWM where lower duty means more action.
    // So we subtract the PID output from the max PWM value.
    float pid_output = pid_controller_calculate(&pressure_pid, target_pressure, current_pressure);
    
    // Invert PID output for the pump
    uint16_t pwm_duty = NPWT_PWM_MAX - (uint16_t)pid_output;

    if (pwm_duty > NPWT_PWM_MAX) pwm_duty = NPWT_PWM_MAX;
    if (pwm_duty < NPWT_PWM_WORKING_MIN) pwm_duty = NPWT_PWM_WORKING_MIN;

    hal_pump_set_duty(pwm_duty);
    g_npwt_system.realtime.pump_pwm = pwm_duty;
}

static void run_intermittent_mode() {
    uint32_t work_time_sec = g_npwt_system.settings.work_time * 60;
    uint32_t rest_time_sec = g_npwt_system.settings.rest_time * 60;

    // State transitions
    if (g_npwt_system.realtime.state == NPWT_STATE_WORKING && g_npwt_system.realtime.work_elapsed >= work_time_sec) {
        g_npwt_system.realtime.state = NPWT_STATE_RESTING;
        g_npwt_system.realtime.rest_elapsed = 0;
    } else if (g_npwt_system.realtime.state == NPWT_STATE_RESTING && g_npwt_system.realtime.rest_elapsed >= rest_time_sec) {
        g_npwt_system.realtime.state = NPWT_STATE_WORKING;
        g_npwt_system.realtime.work_elapsed = 0;
        pid_controller_reset(&pressure_pid); // Reset PID on new cycle
    } else if (g_npwt_system.realtime.state == NPWT_STATE_IDLE) {
        g_npwt_system.realtime.state = NPWT_STATE_WORKING;
        g_npwt_system.realtime.work_elapsed = 0;
    }

    // Actions based on state
    if (g_npwt_system.realtime.state == NPWT_STATE_WORKING) {
        run_continuous_mode(); // Re-use the continuous mode logic for pumping
    } else { // Resting state
        hal_pump_set_duty(NPWT_PWM_MAX);
        g_npwt_system.realtime.pump_pwm = NPWT_PWM_MAX;
    }
}

