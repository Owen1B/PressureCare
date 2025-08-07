/*
 * app_config.h
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#ifndef MAIN_APP_CONFIG_H_
#define MAIN_APP_CONFIG_H_

// I2C Configuration
#define NPWT_I2C_SDA_GPIO           8
#define NPWT_I2C_SCL_GPIO           9
#define NPWT_I2C_FREQ_HZ            100000
#define NPWT_I2C_TIMEOUT_MS         1000

// ADC (Pressure Sensor) Configuration
#define NPWT_ADC_UNIT               ADC_UNIT_1
#define NPWT_ADC_CHANNEL            ADC_CHANNEL_5
#define NPWT_ADC_ATTEN              ADC_ATTEN_DB_12

// Sensor Calibration
#define NPWT_ZERO_POINT_VOLTAGE     480.0f
#define NPWT_PRESSURE_COEFFICIENT   -0.0495f

// PWM (Pump Control) Configuration
#define NPWT_PCA9685_ADDR           0x40
#define NPWT_PCA9685_PWM_CHANNEL    0
#define NPWT_PWM_MAX                4095
#define NPWT_PWM_STARTUP            0
#define NPWT_PWM_WORKING_MIN        0
#define NPWT_PWM_WORKING_MAX        1638 // 40% duty cycle

// PID Controller Parameters
#define NPWT_PID_KP                 20.0f
#define NPWT_PID_KI                 2.0f
#define NPWT_PID_KD                 1.0f

// Kalman Filter Parameters
#define NPWT_KALMAN_Q               0.01f
#define NPWT_KALMAN_R               0.1f

// System Behavior
#define NPWT_PRESSURE_MIN           -30
#define NPWT_PRESSURE_MAX           -10
#define NPWT_PRESSURE_DEFAULT       -20
#define NPWT_TIME_MIN               1
#define NPWT_TIME_MAX               120
#define NPWT_TIME_WORK_DEFAULT      5
#define NPWT_TIME_REST_DEFAULT      2

// NVS Storage Keys
#define NVS_NAMESPACE               "npwt_settings"
#define NVS_KEY_PRESSURE            "target_pressure"
#define NVS_KEY_WORK_TIME           "work_time"
#define NVS_KEY_REST_TIME           "rest_time"
#define NVS_KEY_MODE                "mode"

#endif /* MAIN_APP_CONFIG_H_ */

