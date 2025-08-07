/*
 * hal_pump.h
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#ifndef MAIN_HAL_HAL_PUMP_H_
#define MAIN_HAL_HAL_PUMP_H_

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Initializes the pump control system.
 *
 * This function initializes the I2C bus (if not already done) and the
 * PCA9685 PWM controller.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Failed to initialize
 */
esp_err_t hal_pump_init(void);

/**
 * @brief Deinitializes the pump control system.
 *
 * @return
 *      - ESP_OK: Success
 */
esp_err_t hal_pump_deinit(void);

/**
 * @brief Sets the pump's PWM duty cycle.
 *
 * @param[in] duty The duty cycle to set (0-4095).
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Duty cycle out of range
 *      - ESP_FAIL: I2C communication failed
 */
esp_err_t hal_pump_set_duty(uint16_t duty);

/**
 * @brief Sets the pump's PWM frequency.
 *
 * @param[in] frequency_hz The frequency in Hz.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Failed to set frequency
 */
esp_err_t hal_pump_set_frequency(uint16_t frequency_hz);


#endif /* MAIN_HAL_HAL_PUMP_H_ */

