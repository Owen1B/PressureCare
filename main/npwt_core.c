#include "npwt_core.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "NPWT_CORE";

// 全局系统实例
npwt_system_t g_npwt_system = {0};

// NVS存储键值
#define NVS_NAMESPACE "npwt_settings"
#define NVS_KEY_PRESSURE "target_pressure"
#define NVS_KEY_WORK_TIME "work_time"
#define NVS_KEY_REST_TIME "rest_time"
#define NVS_KEY_MODE "mode"

// PCA9685寄存器地址
#define PCA9685_ADDR    0x40
#define PCA9685_MODE1   0x00
#define PCA9685_PRESCALE 0xFE
#define PCA9685_LED0_ON_L 0x06

// 模拟PWM参数 (实际硬件到位后替换)
static uint16_t current_pwm_duty = 0;
static bool pump_enabled = false;

// ADC配置常量
#define NPWT_ADC_UNIT              ADC_UNIT_1
#define NPWT_ADC_CHANNEL           ADC_CHANNEL_5    // GPIO6
#define NPWT_ADC_ATTEN             ADC_ATTEN_DB_12
#define NPWT_ZERO_POINT_VOLTAGE    2500.0f          // 大气压时的电压 (mV)

// ADC相关静态变量
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_calibrated = false;

// 前向声明
static void npwt_control_task(void *pvParameters);
static void npwt_mode_timer_callback(TimerHandle_t xTimer);
static bool npwt_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void npwt_adc_calibration_deinit(adc_cali_handle_t handle);
static esp_err_t npwt_i2c_init(void);
static esp_err_t npwt_pca9685_init(void);

// 系统初始化
esp_err_t npwt_system_init(void) {
    ESP_LOGI(TAG, "Initializing NPWT system...");

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化互斥量
    g_npwt_system.data_mutex = xSemaphoreCreateMutex();
    if (g_npwt_system.data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        return ESP_FAIL;
    }

    // 加载设置
    npwt_settings_load(&g_npwt_system.settings);

    // 初始化PID控制器
    npwt_pid_init(&g_npwt_system.pid);

    // 初始化压力传感器卡尔曼滤波器
    // Q = 0.005 (过程噪声，调整为Q/R=0.05的比值)
    // R = 0.1 (测量噪声协方差)
    // Q/R = 0.05 (更稳定的滤波，类似原始脚本)
    // initial_estimate = -10 (初始估计值，安全范围内)
    npwt_kalman_init(&g_npwt_system.pressure_kalman, 0.005f, 0.1f, -10.0f);

    // 初始化硬件
    ESP_ERROR_CHECK(npwt_adc_init());
    ESP_ERROR_CHECK(npwt_i2c_init());
    ESP_ERROR_CHECK(npwt_pca9685_init());
    ESP_ERROR_CHECK(npwt_pwm_init());

    // 初始化实时数据
    g_npwt_system.realtime.state = NPWT_STATE_IDLE;
    g_npwt_system.realtime.current_pressure = 0;
    g_npwt_system.realtime.pump_pwm = 0;
    g_npwt_system.realtime.seal_quality = 0;

    // 创建模式切换定时器
    g_npwt_system.mode_timer = xTimerCreate(
        "npwt_mode_timer",
        pdMS_TO_TICKS(1000), // 1秒
        pdTRUE,
        NULL,
        npwt_mode_timer_callback
    );

    g_npwt_system.hardware_ready = true;
    ESP_LOGI(TAG, "NPWT system initialized successfully");

    return ESP_OK;
}

// 系统启动
esp_err_t npwt_system_start(void) {
    ESP_LOGI(TAG, "Starting NPWT system...");

    // 创建控制任务
    BaseType_t ret = xTaskCreatePinnedToCore(
        npwt_control_task,
        "npwt_control",
        4096,
        NULL,
        5,
        &g_npwt_system.control_task,
        1 // 固定在核心1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control task");
        return ESP_FAIL;
    }

    // 启动模式定时器
    xTimerStart(g_npwt_system.mode_timer, 0);

    ESP_LOGI(TAG, "NPWT system started successfully");
    return ESP_OK;
}

// 系统停止
esp_err_t npwt_system_stop(void) {
    ESP_LOGI(TAG, "Stopping NPWT system...");

    // 停止泵
    npwt_pwm_set_duty(0);
    pump_enabled = false;

    // 停止定时器
    if (g_npwt_system.mode_timer) {
        xTimerStop(g_npwt_system.mode_timer, 0);
    }

    // 删除控制任务
    if (g_npwt_system.control_task) {
        vTaskDelete(g_npwt_system.control_task);
        g_npwt_system.control_task = NULL;
    }

    // 更新状态
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.realtime.state = NPWT_STATE_IDLE;
    g_npwt_system.settings.power_on = false;
    xSemaphoreGive(g_npwt_system.data_mutex);

    ESP_LOGI(TAG, "NPWT system stopped");
    return ESP_OK;
}

// 控制任务
static void npwt_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Control task started");

    while (1) {
        xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);

        if (g_npwt_system.settings.power_on) {
            // 每500ms读取一次压力（降低读取频率以减少噪声）
            static uint32_t last_pressure_read = 0;
            uint32_t current_time = esp_timer_get_time() / 1000;
            if (current_time - last_pressure_read >= 500) {
                last_pressure_read = current_time;
                g_npwt_system.realtime.current_pressure = npwt_adc_read_pressure();
            }

            // 执行相应的工作模式
            switch (g_npwt_system.settings.mode) {
                case NPWT_MODE_CONTINUOUS:
                    npwt_mode_continuous_run();
                    break;
                case NPWT_MODE_INTERMITTENT:
                    npwt_mode_intermittent_run();
                    break;
                case NPWT_MODE_DYNAMIC:
                    npwt_mode_dynamic_run();
                    break;
            }

            // 安全检查
            if (!npwt_safety_check()) {
                g_npwt_system.realtime.state = NPWT_STATE_ERROR;
                g_npwt_system.realtime.alarm_active = true;
                npwt_pwm_set_duty(0);
            }
        } else {
            // 系统关闭，停止泵
            npwt_pwm_set_duty(0);
            g_npwt_system.realtime.state = NPWT_STATE_IDLE;
            g_npwt_system.realtime.pump_pwm = 0;
        }

        xSemaphoreGive(g_npwt_system.data_mutex);

        // 调用UI更新回调
        if (g_npwt_system.ui_update_callback) {
            g_npwt_system.ui_update_callback();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms循环
    }
}

// 模式切换定时器回调
static void npwt_mode_timer_callback(TimerHandle_t xTimer) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);

    if (g_npwt_system.settings.power_on) {
        // 更新工作/休息时间计数
        if (g_npwt_system.realtime.state == NPWT_STATE_WORKING) {
            g_npwt_system.realtime.work_elapsed++;
            // 只每10秒输出一次日志，减少栈使用
            if (g_npwt_system.realtime.work_elapsed % 10 == 0) {
                ESP_LOGI(TAG, "Timer: WORKING, elapsed=%lu", g_npwt_system.realtime.work_elapsed);
            }
        } else if (g_npwt_system.realtime.state == NPWT_STATE_RESTING) {
            g_npwt_system.realtime.rest_elapsed++;
            // 只每10秒输出一次日志，减少栈使用
            if (g_npwt_system.realtime.rest_elapsed % 10 == 0) {
                ESP_LOGI(TAG, "Timer: RESTING, elapsed=%lu", g_npwt_system.realtime.rest_elapsed);
            }
        }
    }

    xSemaphoreGive(g_npwt_system.data_mutex);
}

// ADC初始化
esp_err_t npwt_adc_init(void) {
    ESP_LOGI(TAG, "Initializing ADC for pressure sensor...");

    // ADC单元初始化
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = NPWT_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    // ADC通道配置
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = NPWT_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(adc_handle, NPWT_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }

    // ADC校准初始化
    adc_calibrated = npwt_adc_calibration_init(NPWT_ADC_UNIT, NPWT_ADC_CHANNEL, NPWT_ADC_ATTEN, &adc_cali_handle);
    if (adc_calibrated) {
        ESP_LOGI(TAG, "ADC calibration successful, Zero point: %.1f mV", NPWT_ZERO_POINT_VOLTAGE);
    } else {
        ESP_LOGW(TAG, "ADC calibration failed, using raw values, Zero point: %.1f mV", NPWT_ZERO_POINT_VOLTAGE);
    }

    ESP_LOGI(TAG, "ADC initialization completed");
    return ESP_OK;
}

// ADC校准初始化
static bool npwt_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    ESP_LOGI(TAG, "ADC calibration scheme: Curve Fitting");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
        ESP_LOGI(TAG, "ADC calibration successful");
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory for ADC calibration");
    }

    *out_handle = handle;
    return calibrated;
}

// ADC校准反初始化
static void npwt_adc_calibration_deinit(adc_cali_handle_t handle) {
    if (handle != NULL) {
        ESP_LOGI(TAG, "Deregister ADC calibration scheme");
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
    }
}

// 读取压力传感器值
int16_t npwt_adc_read_pressure(void) {
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC not initialized");
        return 0;
    }

    int adc_raw = 0;
    int voltage_mv = 0;
    
    // 读取原始ADC值
    esp_err_t ret = adc_oneshot_read(adc_handle, NPWT_ADC_CHANNEL, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return 0;
    }

    // 校准电压值
    if (adc_calibrated) {
        ret = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to calibrate voltage: %s", esp_err_to_name(ret));
            return 0;
        }
    } else {
        // 如果没有校准，使用简单的线性转换
        voltage_mv = (adc_raw * 3300) / 4095; // 假设3.3V参考电压，12位ADC
    }

    // 计算相对压力（以当前大气压为零点）
    // 新传感器: 大气压2500mV, -98kPa时200mV
    // 斜率 = -98kPa / (200mV - 2500mV) = -98 / (-2300) = 0.0426 kPa/mV
    float relative_pressure_kpa = (voltage_mv - NPWT_ZERO_POINT_VOLTAGE) * 0.0426f;
    
    // 使用卡尔曼滤波器对压力值进行滤波（单位：kPa）
    float filtered_pressure_kpa = npwt_kalman_update(&g_npwt_system.pressure_kalman, relative_pressure_kpa);
    
    // 四舍五入到最接近的1kPa
    int16_t rounded_pressure = (int16_t)(filtered_pressure_kpa + (filtered_pressure_kpa >= 0 ? 0.5f : -0.5f));
    
    // 限制压力范围
    if (rounded_pressure < NPWT_PRESSURE_MIN) {
        rounded_pressure = NPWT_PRESSURE_MIN;
    }
    if (rounded_pressure > NPWT_PRESSURE_MAX) {
        rounded_pressure = NPWT_PRESSURE_MAX;
    }

    static uint32_t last_log_time = 0;
    uint32_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
    
    // 每1秒输出一次日志
    if (current_time - last_log_time >= 1000) {
        last_log_time = current_time;
        ESP_LOGI(TAG, "Raw: %.2f kPa, Filtered: %.2f kPa, Rounded: %d kPa", 
                 relative_pressure_kpa, filtered_pressure_kpa, rounded_pressure);
    }

    return rounded_pressure;
}

// I2C初始化
static esp_err_t npwt_i2c_init(void) {
    ESP_LOGI(TAG, "Initializing I2C for PCA9685...");

    // 暂时空着，等硬件到位后再实现

    return ESP_OK;
}

// PCA9685初始化
static esp_err_t npwt_pca9685_init(void) {
    ESP_LOGI(TAG, "Initializing PCA9685 PWM driver...");

    // 这里应该是真实的PCA9685初始化代码
    // 实际硬件到位后替换

    return ESP_OK;
}

// PWM初始化
esp_err_t npwt_pwm_init(void) {
    ESP_LOGI(TAG, "Initializing PWM for pump control...");

    // 初始化PCA9685或ESP32内部PWM
    // 实际硬件到位后替换

    return ESP_OK;
}

// 设置PWM占空比
esp_err_t npwt_pwm_set_duty(uint16_t duty) {
    if (duty > NPWT_PWM_MAX) {
        duty = NPWT_PWM_MAX;
    }

    current_pwm_duty = duty;
    pump_enabled = (duty > 0);

    // 这里应该是真实的PWM设置代码
    // 实际硬件到位后替换为PCA9685写入代码

    g_npwt_system.realtime.pump_pwm = duty;

    ESP_LOGD(TAG, "PWM duty set to: %d", duty);
    return ESP_OK;
}

// PID控制器初始化
esp_err_t npwt_pid_init(npwt_pid_t *pid) {
    if (!pid) return ESP_ERR_INVALID_ARG;

    // PID参数 (可根据实际系统调整)
    pid->kp = 2.0f;
    pid->ki = 0.1f;
    pid->kd = 0.05f;

    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->output_min = 0.0f;
    pid->output_max = NPWT_PWM_MAX;
    pid->last_time = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "PID controller initialized (Kp=%.2f, Ki=%.2f, Kd=%.2f)",
             pid->kp, pid->ki, pid->kd);

    return ESP_OK;
}

// PID计算
float npwt_pid_calculate(npwt_pid_t *pid, float setpoint, float input) {
    if (!pid) return 0.0f;

    uint32_t current_time = esp_timer_get_time() / 1000;
    float dt = (current_time - pid->last_time) / 1000.0f;

    if (dt <= 0.0f) return pid->output_min;

    float error = setpoint - input;

    // 积分项
    pid->integral += error * dt;

    // 积分饱和保护
    float integral_max = pid->output_max / pid->ki;
    if (pid->integral > integral_max) pid->integral = integral_max;
    if (pid->integral < -integral_max) pid->integral = -integral_max;

    // 微分项
    float derivative = (error - pid->prev_error) / dt;

    // PID输出
    float output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;

    // 输出限制
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    pid->prev_error = error;
    pid->last_time = current_time;

    return output;
}

// 持续模式运行
esp_err_t npwt_mode_continuous_run(void) {
    g_npwt_system.realtime.state = NPWT_STATE_WORKING;

    // 使用PID控制器调节泵速
    float pid_output = npwt_pid_calculate(
        &g_npwt_system.pid,
        g_npwt_system.settings.target_pressure,
        g_npwt_system.realtime.current_pressure
    );

    npwt_pwm_set_duty((uint16_t)pid_output);

    return ESP_OK;
}

// 间歇模式运行
esp_err_t npwt_mode_intermittent_run(void) {
    uint32_t work_time_sec = g_npwt_system.settings.work_time * 60;
    uint32_t rest_time_sec = g_npwt_system.settings.rest_time * 60;

    if (g_npwt_system.realtime.state == NPWT_STATE_WORKING) {
        if (g_npwt_system.realtime.work_elapsed >= work_time_sec) {
            // 切换到休息状态
            g_npwt_system.realtime.state = NPWT_STATE_RESTING;
            g_npwt_system.realtime.work_elapsed = 0;
            g_npwt_system.realtime.rest_elapsed = 0;
            npwt_pwm_set_duty(0);
        } else {
            // 继续工作
            float pid_output = npwt_pid_calculate(
                &g_npwt_system.pid,
                g_npwt_system.settings.target_pressure,
                g_npwt_system.realtime.current_pressure
            );
            npwt_pwm_set_duty((uint16_t)pid_output);
        }
    } else if (g_npwt_system.realtime.state == NPWT_STATE_RESTING) {
        if (g_npwt_system.realtime.rest_elapsed >= rest_time_sec) {
            // 切换到工作状态
            g_npwt_system.realtime.state = NPWT_STATE_WORKING;
            g_npwt_system.realtime.work_elapsed = 0;
            g_npwt_system.realtime.rest_elapsed = 0;
            npwt_pid_reset(&g_npwt_system.pid);
        } else {
            // 继续休息
            npwt_pwm_set_duty(0);
        }
    } else {
        // 初始状态，开始工作
        g_npwt_system.realtime.state = NPWT_STATE_WORKING;
        g_npwt_system.realtime.work_elapsed = 0;
        g_npwt_system.realtime.rest_elapsed = 0;
        npwt_pid_reset(&g_npwt_system.pid);
    }

    return ESP_OK;
}

// 动态模式运行 (斜坡下降)
esp_err_t npwt_mode_dynamic_run(void) {
    uint32_t work_time_sec = g_npwt_system.settings.work_time * 60;
    uint32_t rest_time_sec = g_npwt_system.settings.rest_time * 60;

    if (g_npwt_system.realtime.state == NPWT_STATE_WORKING) {
        if (g_npwt_system.realtime.work_elapsed >= work_time_sec) {
            // 切换到休息状态
            g_npwt_system.realtime.state = NPWT_STATE_RESTING;
            g_npwt_system.realtime.work_elapsed = 0;
            g_npwt_system.realtime.rest_elapsed = 0;
        } else {
            // 工作阶段：从目标压力逐渐上升到0
            float progress = (float)g_npwt_system.realtime.work_elapsed / work_time_sec;
            float ramp_target = g_npwt_system.settings.target_pressure * (1.0f - progress);

            // 每10秒输出一次dynamic work日志
            if (g_npwt_system.realtime.work_elapsed % 10 == 0) {
                ESP_LOGI(TAG, "Dynamic WORK: elapsed=%lu, progress=%.2f, target=%.1f",
                    g_npwt_system.realtime.work_elapsed, progress, ramp_target);
            }

            float pid_output = npwt_pid_calculate(
                &g_npwt_system.pid,
                ramp_target,
                g_npwt_system.realtime.current_pressure
            );
            npwt_pwm_set_duty((uint16_t)pid_output);
        }
    } else if (g_npwt_system.realtime.state == NPWT_STATE_RESTING) {
        if (g_npwt_system.realtime.rest_elapsed >= rest_time_sec) {
            // 切换到工作状态
            g_npwt_system.realtime.state = NPWT_STATE_WORKING;
            g_npwt_system.realtime.work_elapsed = 0;
            g_npwt_system.realtime.rest_elapsed = 0;
            npwt_pid_reset(&g_npwt_system.pid);
        } else {
            // 休息阶段：从0逐渐下降到目标压力
            float progress = (float)g_npwt_system.realtime.rest_elapsed / rest_time_sec;
            float ramp_target = g_npwt_system.settings.target_pressure * progress;

            // 每10秒输出一次dynamic rest日志
            if (g_npwt_system.realtime.rest_elapsed % 10 == 0) {
                ESP_LOGI(TAG, "Dynamic REST: elapsed=%lu, progress=%.2f, target=%.1f",
                    g_npwt_system.realtime.rest_elapsed, progress, ramp_target);
            }

            float pid_output = npwt_pid_calculate(
                &g_npwt_system.pid,
                ramp_target,
                g_npwt_system.realtime.current_pressure
            );
            npwt_pwm_set_duty((uint16_t)pid_output);
        }
    } else {
        // 初始状态，开始工作
        g_npwt_system.realtime.state = NPWT_STATE_WORKING;
        g_npwt_system.realtime.work_elapsed = 0;
        g_npwt_system.realtime.rest_elapsed = 0;
        npwt_pid_reset(&g_npwt_system.pid);
    }

    return ESP_OK;
}

// 安全检查
bool npwt_safety_check(void) {
    // 检查压力是否在安全范围内，只记录警告，不影响正常使用
    if (g_npwt_system.realtime.current_pressure < NPWT_PRESSURE_MIN ||
        g_npwt_system.realtime.current_pressure > NPWT_PRESSURE_MAX) {
        ESP_LOGW(TAG, "Pressure out of safe range: %d kPa",
                 g_npwt_system.realtime.current_pressure);
        // 不返回 false，继续运行
    }

    // 检查密封质量
    if (g_npwt_system.realtime.seal_quality < 50) {
        ESP_LOGW(TAG, "Poor seal quality: %d%%", g_npwt_system.realtime.seal_quality);
        return false;
    }

    return true;
}

// 设置目标压力
esp_err_t npwt_set_target_pressure(int16_t pressure) {
    if (pressure < NPWT_PRESSURE_MIN || pressure > NPWT_PRESSURE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.target_pressure = pressure;
    xSemaphoreGive(g_npwt_system.data_mutex);

    ESP_LOGI(TAG, "Target pressure set to: %d kPa", pressure);
    return ESP_OK;
}

// 设置工作模式
esp_err_t npwt_set_mode(npwt_mode_t mode) {
    if (mode >= 3) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.mode = mode;

    // 重置状态
    g_npwt_system.realtime.state = NPWT_STATE_IDLE;
    g_npwt_system.realtime.work_elapsed = 0;
    g_npwt_system.realtime.rest_elapsed = 0;
    npwt_pid_reset(&g_npwt_system.pid);

    xSemaphoreGive(g_npwt_system.data_mutex);

    const char *mode_names[] = {"Continuous", "Intermittent", "Dynamic"};
    ESP_LOGI(TAG, "Mode set to: %s", mode_names[mode]);
    return ESP_OK;
}

// 设置工作时间
esp_err_t npwt_set_work_time(uint8_t minutes) {
    if (minutes < NPWT_TIME_MIN || minutes > NPWT_TIME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.work_time = minutes;

    // 重置状态，使新的时间设置立即生效
    g_npwt_system.realtime.state = NPWT_STATE_IDLE;
    g_npwt_system.realtime.work_elapsed = 0;
    g_npwt_system.realtime.rest_elapsed = 0;
    npwt_pid_reset(&g_npwt_system.pid);

    xSemaphoreGive(g_npwt_system.data_mutex);

    ESP_LOGI(TAG, "Work time set to: %d minutes", minutes);
    return ESP_OK;
}

// 设置休息时间
esp_err_t npwt_set_rest_time(uint8_t minutes) {
    if (minutes < NPWT_TIME_MIN || minutes > NPWT_TIME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.rest_time = minutes;

    // 重置状态，使新的时间设置立即生效
    g_npwt_system.realtime.state = NPWT_STATE_IDLE;
    g_npwt_system.realtime.work_elapsed = 0;
    g_npwt_system.realtime.rest_elapsed = 0;
    npwt_pid_reset(&g_npwt_system.pid);

    xSemaphoreGive(g_npwt_system.data_mutex);

    ESP_LOGI(TAG, "Rest time set to: %d minutes", minutes);
    return ESP_OK;
}

// 设置电源状态
esp_err_t npwt_set_power(bool power_on) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    g_npwt_system.settings.power_on = power_on;

    if (!power_on) {
        g_npwt_system.realtime.state = NPWT_STATE_IDLE;
        g_npwt_system.realtime.work_elapsed = 0;
        g_npwt_system.realtime.rest_elapsed = 0;
        npwt_pid_reset(&g_npwt_system.pid);
    }

    xSemaphoreGive(g_npwt_system.data_mutex);

    ESP_LOGI(TAG, "Power %s", power_on ? "ON" : "OFF");
    return ESP_OK;
}

// 获取当前压力
int16_t npwt_get_current_pressure(void) {
    return g_npwt_system.realtime.current_pressure;
}

// 获取系统设置
npwt_settings_t npwt_get_settings(void) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    npwt_settings_t settings = g_npwt_system.settings;
    xSemaphoreGive(g_npwt_system.data_mutex);
    return settings;
}

// 获取实时数据
npwt_realtime_t npwt_get_realtime_data(void) {
    xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);
    npwt_realtime_t realtime = g_npwt_system.realtime;
    xSemaphoreGive(g_npwt_system.data_mutex);
    return realtime;
}

// 加载设置
esp_err_t npwt_settings_load(npwt_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS, using default settings");
        npwt_settings_reset_default(settings);
        return ESP_OK;
    }

    size_t required_size = sizeof(int16_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_PRESSURE, &settings->target_pressure, &required_size);
    if (err != ESP_OK || settings->target_pressure < NPWT_PRESSURE_MIN || settings->target_pressure > NPWT_PRESSURE_MAX) {
        ESP_LOGW(TAG, "Invalid pressure value %d, resetting to default %d", settings->target_pressure, NPWT_PRESSURE_DEFAULT);
        settings->target_pressure = NPWT_PRESSURE_DEFAULT;
    }

    required_size = sizeof(uint8_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_WORK_TIME, &settings->work_time, &required_size);
    if (err != ESP_OK) settings->work_time = NPWT_TIME_WORK_DEFAULT;

    required_size = sizeof(uint8_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_REST_TIME, &settings->rest_time, &required_size);
    if (err != ESP_OK) settings->rest_time = NPWT_TIME_REST_DEFAULT;

    required_size = sizeof(uint8_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_MODE, &settings->mode, &required_size);
    if (err != ESP_OK) settings->mode = NPWT_MODE_CONTINUOUS;

    settings->power_on = false; // 开机默认关闭

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Settings loaded: pressure=%d, work_time=%d, rest_time=%d, mode=%d",
             settings->target_pressure, settings->work_time, settings->rest_time, settings->mode);

    return ESP_OK;
}

// 保存设置
esp_err_t npwt_settings_save(const npwt_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return err;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY_PRESSURE, &settings->target_pressure, sizeof(int16_t));
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_blob(nvs_handle, NVS_KEY_WORK_TIME, &settings->work_time, sizeof(uint8_t));
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_blob(nvs_handle, NVS_KEY_REST_TIME, &settings->rest_time, sizeof(uint8_t));
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_blob(nvs_handle, NVS_KEY_MODE, &settings->mode, sizeof(uint8_t));
    if (err != ESP_OK) goto cleanup;

    err = nvs_commit(nvs_handle);

cleanup:
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Settings saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to save settings");
    }

    return err;
}

// 重置默认设置
esp_err_t npwt_settings_reset_default(npwt_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;

    settings->target_pressure = NPWT_PRESSURE_DEFAULT;
    settings->work_time = NPWT_TIME_WORK_DEFAULT;
    settings->rest_time = NPWT_TIME_REST_DEFAULT;
    settings->mode = NPWT_MODE_CONTINUOUS;
    settings->power_on = false;

    ESP_LOGI(TAG, "Settings reset to default");
    return ESP_OK;
}

// 注册UI回调
void npwt_register_ui_callback(void (*callback)(void)) {
    g_npwt_system.ui_update_callback = callback;
}

// 获取当前目标压力（动态模式下会变化）
int16_t npwt_get_current_target_pressure(void) {
    if (g_npwt_system.settings.mode == NPWT_MODE_DYNAMIC) {
        if (g_npwt_system.realtime.state == NPWT_STATE_WORKING) {
            // 工作阶段：从目标压力逐渐上升到0
            uint32_t work_time_sec = g_npwt_system.settings.work_time * 60;
            float progress = (float)g_npwt_system.realtime.work_elapsed / work_time_sec;
            float ramp_target = g_npwt_system.settings.target_pressure * (1.0f - progress);

            return (int16_t)ramp_target;
        } else if (g_npwt_system.realtime.state == NPWT_STATE_RESTING) {
            // 休息阶段：从0逐渐下降到目标压力
            uint32_t rest_time_sec = g_npwt_system.settings.rest_time * 60;
            float progress = (float)g_npwt_system.realtime.rest_elapsed / rest_time_sec;
            float ramp_target = g_npwt_system.settings.target_pressure * progress;

            return (int16_t)ramp_target;
        }
    } else if (g_npwt_system.settings.mode == NPWT_MODE_INTERMITTENT) {
        // 间歇模式：工作阶段返回设定压力，休息阶段返回0
        if (g_npwt_system.realtime.state == NPWT_STATE_WORKING) {
            return g_npwt_system.settings.target_pressure;
        } else if (g_npwt_system.realtime.state == NPWT_STATE_RESTING) {
            return 0; // 休息阶段目标压力为0
        }
    }

    // 持续模式或其他状态，返回设定的目标压力
    return g_npwt_system.settings.target_pressure;
}

// PID重置
void npwt_pid_reset(npwt_pid_t *pid) {
    if (!pid) return;

    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->last_time = esp_timer_get_time() / 1000;
}

// 卡尔曼滤波器初始化
esp_err_t npwt_kalman_init(npwt_kalman_t *kalman, float Q, float R, float initial_estimate) {
    if (!kalman) return ESP_ERR_INVALID_ARG;

    kalman->x = initial_estimate;   // 初始状态估计
    kalman->P = 1.0f;               // 初始估计协方差
    kalman->Q = Q;                  // 过程噪声协方差
    kalman->R = R;                  // 测量噪声协方差
    kalman->K = 0.0f;               // 卡尔曼增益
    kalman->initialized = true;

    ESP_LOGI(TAG, "Kalman filter initialized: Q=%.3f, R=%.3f, initial=%.1f", Q, R, initial_estimate);
    return ESP_OK;
}

// 卡尔曼滤波器更新
float npwt_kalman_update(npwt_kalman_t *kalman, float measurement) {
    if (!kalman || !kalman->initialized) return measurement;

    // 预测步骤
    // x_pred = x (假设是常量模型)
    // P_pred = P + Q
    float P_pred = kalman->P + kalman->Q;

    // 更新步骤
    // K = P_pred / (P_pred + R)
    kalman->K = P_pred / (P_pred + kalman->R);

    // x = x_pred + K * (measurement - x_pred)
    kalman->x = kalman->x + kalman->K * (measurement - kalman->x);

    // P = (1 - K) * P_pred
    kalman->P = (1.0f - kalman->K) * P_pred;

    return kalman->x;
}

// 密封检查 (模拟)
esp_err_t npwt_seal_check(void) {
    // 模拟密封质量检查
    // 实际硬件到位后替换为真实的检查逻辑

    static uint8_t seal_quality = 85; // 模拟85%密封质量
    g_npwt_system.realtime.seal_quality = seal_quality;

    return ESP_OK;
}

// 系统反初始化
esp_err_t npwt_system_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing NPWT system...");

    // 停止所有任务和定时器
    if (g_npwt_system.control_task) {
        vTaskDelete(g_npwt_system.control_task);
        g_npwt_system.control_task = NULL;
    }

    if (g_npwt_system.mode_timer) {
        xTimerStop(g_npwt_system.mode_timer, portMAX_DELAY);
        xTimerDelete(g_npwt_system.mode_timer, portMAX_DELAY);
        g_npwt_system.mode_timer = NULL;
    }

    // 清理ADC资源
    if (adc_calibrated && adc_cali_handle) {
        npwt_adc_calibration_deinit(adc_cali_handle);
        adc_cali_handle = NULL;
        adc_calibrated = false;
    }

    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    // 清理互斥锁
    if (g_npwt_system.data_mutex) {
        vSemaphoreDelete(g_npwt_system.data_mutex);
        g_npwt_system.data_mutex = NULL;
    }

    // 重置系统状态
    memset(&g_npwt_system, 0, sizeof(npwt_system_t));

    ESP_LOGI(TAG, "NPWT system deinitialized");
    return ESP_OK;
}
