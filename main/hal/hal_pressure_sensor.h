/*
 * hal_pressure_sensor.h
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#ifndef MAIN_HAL_HAL_PRESSURE_SENSOR_H_
#define MAIN_HAL_HAL_PRESSURE_SENSOR_H_

#include "esp_err.h"

/**
 * @brief Initializes the pressure sensor.
 *
 * This function sets up the ADC unit and channel for the pressure sensor,
 * and performs calibration if possible.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Failed to initialize
 */
esp_err_t hal_pressure_sensor_init(void);

/**
 * @brief Deinitializes the pressure sensor.
 *
 * This function releases the resources used by the ADC and calibration.
 *
 * @return
 *      - ESP_OK: Success
 */
esp_err_t hal_pressure_sensor_deinit(void);

/**
 * @brief Reads the pressure from the sensor.
 *
 * This function reads the raw ADC value, converts it to voltage, applies calibration,
 * and then calculates the pressure in kPa.
 *
 * @param[out] pressure_kpa Pointer to store the resulting pressure in kPa.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: ADC read failed
 */
esp_err_t hal_pressure_sensor_read(int16_t *pressure_kpa);

#endif /* MAIN_HAL_HAL_PRESSURE_SENSOR_H_ */

