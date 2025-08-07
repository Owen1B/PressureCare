/*
 * kalman_filter.c
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#include "algorithms/kalman_filter.h"
#include "esp_log.h"

static const char *TAG = "KALMAN_FILTER";

esp_err_t kalman_filter_init(kalman_filter_t *kf, float Q, float R, float initial_estimate) {
    if (!kf) {
        return ESP_ERR_INVALID_ARG;
    }

    kf->x = initial_estimate;
    kf->P = 1.0f; // Initial error covariance
    kf->Q = Q;
    kf->R = R;
    kf->K = 0.0f;
    kf->initialized = true;

    ESP_LOGI(TAG, "Kalman filter initialized (Q=%.3f, R=%.3f)", Q, R);
    return ESP_OK;
}

float kalman_filter_update(kalman_filter_t *kf, float measurement) {
    if (!kf || !kf->initialized) {
        return measurement; // Return raw measurement if not initialized
    }

    // Prediction update
    // x = x (since we have a static model)
    kf->P = kf->P + kf->Q;

    // Measurement update
    kf->K = kf->P / (kf->P + kf->R);
    kf->x = kf->x + kf->K * (measurement - kf->x);
    kf->P = (1.0f - kf->K) * kf->P;

    return kf->x;
}

