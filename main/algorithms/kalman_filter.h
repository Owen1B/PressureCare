/*
 * kalman_filter.h
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#ifndef MAIN_ALGORITHMS_KALMAN_FILTER_H_
#define MAIN_ALGORITHMS_KALMAN_FILTER_H_

#include "esp_err.h"

typedef struct {
    float x; // state
    float P; // error covariance
    float Q; // process noise covariance
    float R; // measurement noise covariance
    float K; // kalman gain
    bool initialized;
} kalman_filter_t;

/**
 * @brief Initializes a Kalman filter.
 *
 * @param[out] kf Pointer to the Kalman filter instance.
 * @param[in] Q Process noise covariance.
 * @param[in] R Measurement noise covariance.
 * @param[in] initial_estimate Initial estimate of the state.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t kalman_filter_init(kalman_filter_t *kf, float Q, float R, float initial_estimate);

/**
 * @brief Updates the Kalman filter with a new measurement.
 *
 * @param[in,out] kf Pointer to the Kalman filter instance.
 * @param[in] measurement The new measurement.
 * @return The filtered (estimated) state.
 */
float kalman_filter_update(kalman_filter_t *kf, float measurement);

#endif /* MAIN_ALGORITHMS_KALMAN_FILTER_H_ */

