/*
 * pid_controller.h
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#ifndef MAIN_ALGORITHMS_PID_CONTROLLER_H_
#define MAIN_ALGORITHMS_PID_CONTROLLER_H_

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral;
    float prev_error;
    float output_min;
    float output_max;
    uint32_t last_time;
} pid_controller_t;

/**
 * @brief Initializes a PID controller.
 *
 * @param[out] pid Pointer to the PID controller instance.
 * @param[in] kp Proportional gain.
 * @param[in] ki Integral gain.
 * @param[in] kd Derivative gain.
 * @param[in] output_min Minimum output value.
 * @param[in] output_max Maximum output value.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t pid_controller_init(pid_controller_t *pid, float kp, float ki, float kd, float output_min, float output_max);

/**
 * @brief Calculates the PID output.
 *
 * @param[in,out] pid Pointer to the PID controller instance.
 * @param[in] setpoint The desired value.
 * @param[in] input The current measured value.
 * @return The calculated control output.
 */
float pid_controller_calculate(pid_controller_t *pid, float setpoint, float input);

/**
 * @brief Resets the PID controller's state.
 *
 * @param[in,out] pid Pointer to the PID controller instance.
 */
void pid_controller_reset(pid_controller_t *pid);

#endif /* MAIN_ALGORITHMS_PID_CONTROLLER_H_ */

