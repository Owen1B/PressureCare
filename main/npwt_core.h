/*
 * npwt_core.h
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 *
 *  Description: This file defines the core data structures and enums
 *               shared across the NPWT application (logic, UI, etc.).
 */

#ifndef MAIN_NPWT_CORE_H_
#define MAIN_NPWT_CORE_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// System States
typedef enum {
    NPWT_STATE_IDLE,
    NPWT_STATE_WORKING,
    NPWT_STATE_RESTING,
    NPWT_STATE_ERROR,
} npwt_state_t;

// Therapy Modes
typedef enum {
    NPWT_MODE_CONTINUOUS,
    NPWT_MODE_INTERMITTENT,
} npwt_mode_t;

// System Settings
typedef struct {
    int16_t target_pressure;
    uint8_t work_time;      // minutes
    uint8_t rest_time;      // minutes
    npwt_mode_t mode;
    bool power_on;
} npwt_settings_t;

// Real-time Data
typedef struct {
    npwt_state_t state;
    int16_t current_pressure;
    uint16_t pump_pwm;
    uint32_t work_elapsed;  // seconds
    uint32_t rest_elapsed;  // seconds
    bool alarm_active;
} npwt_realtime_t;

// Main System Structure
typedef struct {
    npwt_settings_t settings;
    npwt_realtime_t realtime;
    SemaphoreHandle_t data_mutex;
    void (*ui_update_callback)(void);
} npwt_system_t;


/**
 * @brief Initializes the entire NPWT system.
 */
esp_err_t npwt_system_init(void);

/**
 * @brief Deinitializes the NPWT system.
 */
esp_err_t npwt_system_deinit(void);

/**
 * @brief Starts the NPWT system operation.
 */
esp_err_t npwt_system_start(void);

/**
 * @brief Stops the NPWT system operation.
 */
esp_err_t npwt_system_stop(void);


// --- Data Access and Control Functions ---

void npwt_set_target_pressure(int16_t pressure);
void npwt_set_work_time(uint8_t minutes);
void npwt_set_rest_time(uint8_t minutes);
void npwt_set_mode(npwt_mode_t mode);
void npwt_set_power(bool power_on);

npwt_settings_t npwt_get_settings(void);
npwt_realtime_t npwt_get_realtime_data(void);

void npwt_register_ui_callback(void (*callback)(void));
esp_err_t npwt_settings_save(const npwt_settings_t *settings);


#endif /* MAIN_NPWT_CORE_H_ */
