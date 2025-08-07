/*
 * pid_controller.c
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#include "algorithms/pid_controller.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "PID_CONTROLLER";

esp_err_t pid_controller_init(pid_controller_t *pid, float kp, float ki, float kd, float output_min, float output_max) {
    if (!pid) {
        return ESP_ERR_INVALID_ARG;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->output_min = output_min;
    pid->output_max = output_max;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->last_time = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "PID controller initialized (Kp=%.2f, Ki=%.2f, Kd=%.2f)", pid->kp, pid->ki, pid->kd);
    return ESP_OK;
}

float pid_controller_calculate(pid_controller_t *pid, float setpoint, float input) {
    if (!pid) {
        return 0.0f;
    }

    uint32_t current_time = esp_timer_get_time() / 1000;
    float dt = (current_time - pid->last_time) / 1000.0f;
    if (dt <= 0.001f) {
        dt = 0.001f; // Prevent division by zero
    }

    float error = setpoint - input;

    // Integral term with anti-windup
    pid->integral += error * dt;
    float integral_max = pid->output_max / pid->ki;
    if (pid->integral > integral_max) pid->integral = integral_max;
    if (pid->integral < -integral_max) pid->integral = -integral_max;

    float derivative = (error - pid->prev_error) / dt;

    float output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;

    if (output > pid->output_max) {
        output = pid->output_max;
    }
    if (output < pid->output_min) {
        output = pid->output_min;
    }

    pid->prev_error = error;
    pid->last_time = current_time;
    
    ESP_LOGD(TAG, "Setpoint: %.2f, Input: %.2f, Output: %.2f", setpoint, input, output);

    return output;
}

void pid_controller_reset(pid_controller_t *pid) {
    if (pid) {
        pid->integral = 0.0f;
        pid->prev_error = 0.0f;
        pid->last_time = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "PID controller reset.");
    }
}

