#ifndef NPWT_CORE_H
#define NPWT_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 系统配置常量
#define NPWT_PRESSURE_MIN           -100    // 最小负压值 (kPa)
#define NPWT_PRESSURE_MAX           0       // 最大负压值 (kPa)
#define NPWT_PRESSURE_STEP          -1      // 负压调节步长 (kPa)
#define NPWT_PRESSURE_DEFAULT       -16     // 默认负压值 (kPa, 约-120mmHg)

// I2C和PCA9685配置
#define NPWT_I2C_SDA_GPIO           8       // I2C SDA引脚 (与板子一致)
#define NPWT_I2C_SCL_GPIO           9       // I2C SCL引脚 (与板子一致)
#define NPWT_I2C_FREQ_HZ            100000  // I2C频率 100kHz
#define NPWT_PCA9685_ADDR           0x40    // PCA9685 I2C地址 (默认地址)
#define NPWT_PCA9685_PWM_CHANNEL    0       // 使用PWM通道0
#define NPWT_I2C_TIMEOUT_MS         50      // I2C超时时间

#define NPWT_TIME_MIN               1       // 最小时间 (分钟)
#define NPWT_TIME_MAX               60      // 最大时间 (分钟)
#define NPWT_TIME_WORK_DEFAULT      5       // 默认工作时间
#define NPWT_TIME_REST_DEFAULT      3       // 默认休息时间

#define NPWT_PWM_MIN                0       // PWM最小值
#define NPWT_PWM_MAX                4095    // PWM最大值 (12-bit)
#define NPWT_PWM_STARTUP            3686    // 启动时初始PWM占空比 (90%)
#define NPWT_PWM_WORKING_MAX        3686    // 工作时最大PWM占空比 (90%)
#define NPWT_PWM_WORKING_MIN        2457    // 工作时最小PWM占空比 (60%)
#define NPWT_ADC_SAMPLES            10      // ADC采样次数

// 工作模式枚举
typedef enum {
    NPWT_MODE_CONTINUOUS = 0,   // 持续模式
    NPWT_MODE_INTERMITTENT,     // 间歇模式
    NPWT_MODE_DYNAMIC          // 动态模式
} npwt_mode_t;

// 系统状态枚举
typedef enum {
    NPWT_STATE_IDLE = 0,        // 待机状态
    NPWT_STATE_WORKING,         // 工作状态
    NPWT_STATE_RESTING,         // 休息状态
    NPWT_STATE_SEALING_CHECK,   // 密封检查
    NPWT_STATE_ERROR           // 错误状态
} npwt_state_t;

// 系统参数结构体
typedef struct {
    int16_t target_pressure;    // 目标负压值 (kPa)
    uint8_t work_time;          // 工作时间 (分钟)
    uint8_t rest_time;          // 休息时间 (分钟)
    npwt_mode_t mode;           // 工作模式
    bool power_on;              // 总开关状态
} npwt_settings_t;

// 系统实时数据结构体
typedef struct {
    int16_t current_pressure;   // 当前负压值 (kPa)
    uint16_t pump_pwm;          // 泵PWM值
    uint8_t seal_quality;       // 密封质量 (0-100%)
    float actual_flow;          // 实际流量 (L/min)
    float leakage_flow;         // 漏气流量 (L/min)
    uint32_t work_elapsed;      // 工作时间已过 (秒)
    uint32_t rest_elapsed;      // 休息时间已过 (秒)
    npwt_state_t state;         // 当前状态
    bool alarm_active;          // 报警状态
} npwt_realtime_t;

// PID控制器结构体
typedef struct {
    float kp, ki, kd;           // PID参数
    float integral;             // 积分项
    float prev_error;           // 上次误差
    float output_min, output_max; // 输出限制
    uint32_t last_time;         // 上次计算时间
} npwt_pid_t;

// 卡尔曼滤波器结构体
typedef struct {
    float x;                    // 状态估计值
    float P;                    // 估计协方差
    float Q;                    // 过程噪声协方差
    float R;                    // 测量噪声协方差
    float K;                    // 卡尔曼增益
    bool initialized;           // 是否已初始化
} npwt_kalman_t;

// 系统主控制结构体
typedef struct {
    npwt_settings_t settings;   // 系统设置
    npwt_realtime_t realtime;   // 实时数据
    npwt_pid_t pid;             // PID控制器
    npwt_kalman_t pressure_kalman; // 压力传感器卡尔曼滤波器

    // FreeRTOS对象
    TaskHandle_t control_task;  // 控制任务句柄
    TimerHandle_t mode_timer;   // 模式切换定时器
    SemaphoreHandle_t data_mutex; // 数据保护互斥量

    // 硬件相关
    bool hardware_ready;        // 硬件就绪状态
    uint32_t adc_channel;       // ADC通道
    
    // I2C和PCA9685相关 (使用共享I2C总线)
    bool i2c_initialized;       // I2C是否已初始化

    // 回调函数
    void (*ui_update_callback)(void); // UI更新回调
} npwt_system_t;

// 全局系统实例
extern npwt_system_t g_npwt_system;

// 核心API函数
esp_err_t npwt_system_init(void);
esp_err_t npwt_system_start(void);
esp_err_t npwt_system_stop(void);
esp_err_t npwt_system_deinit(void);

// 参数管理
esp_err_t npwt_settings_load(npwt_settings_t *settings);
esp_err_t npwt_settings_save(const npwt_settings_t *settings);
esp_err_t npwt_settings_reset_default(npwt_settings_t *settings);

// 控制功能
esp_err_t npwt_set_target_pressure(int16_t pressure);
esp_err_t npwt_set_work_time(uint8_t minutes);
esp_err_t npwt_set_rest_time(uint8_t minutes);
esp_err_t npwt_set_mode(npwt_mode_t mode);
esp_err_t npwt_set_power(bool power_on);

// 状态查询
int16_t npwt_get_current_pressure(void);
uint8_t npwt_get_seal_quality(void);
npwt_state_t npwt_get_state(void);
npwt_settings_t npwt_get_settings(void);
npwt_realtime_t npwt_get_realtime_data(void);

// 硬件接口 (模拟)
esp_err_t npwt_adc_init(void);
int16_t npwt_adc_read_pressure(void);
esp_err_t npwt_pwm_init(void);
esp_err_t npwt_pwm_set_duty(uint16_t duty);

// PID控制
esp_err_t npwt_pid_init(npwt_pid_t *pid);
float npwt_pid_calculate(npwt_pid_t *pid, float setpoint, float input);
void npwt_pid_reset(npwt_pid_t *pid);

// 卡尔曼滤波器
esp_err_t npwt_kalman_init(npwt_kalman_t *kalman, float Q, float R, float initial_estimate);
float npwt_kalman_update(npwt_kalman_t *kalman, float measurement);

// 工作模式控制
esp_err_t npwt_mode_continuous_run(void);
esp_err_t npwt_mode_intermittent_run(void);
esp_err_t npwt_mode_dynamic_run(void);

// 安全检查
bool npwt_safety_check(void);

// UI回调注册
void npwt_register_ui_callback(void (*callback)(void));

// 获取当前目标压力（动态模式下会变化）
int16_t npwt_get_current_target_pressure(void);

// 流量计算相关函数
float npwt_calculate_theoretical_flow(float pressure_kpa);
float npwt_calculate_actual_flow(float pressure_kpa, uint16_t pwm_duty);
uint8_t npwt_flow_to_bar_percentage(float flow_lpm);
void npwt_update_flow_analysis(void);

// I2C和PCA9685相关函数
esp_err_t npwt_i2c_init(void);
esp_err_t npwt_pca9685_init(void);
esp_err_t npwt_pca9685_set_pwm(uint8_t channel, uint16_t duty);
esp_err_t npwt_pca9685_set_frequency(uint16_t frequency_hz);
esp_err_t npwt_pca9685_test_output(void);
esp_err_t npwt_i2c_scan_devices(void);
esp_err_t npwt_i2c_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // NPWT_CORE_H
