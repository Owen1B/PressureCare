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
#define NPWT_ZERO_POINT_VOLTAGE    480.0f           // 大气压时的电压 (mV)

// ADC相关静态变量
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_calibrated = false;

// 前向声明
static void npwt_control_task(void *pvParameters);
static void npwt_mode_timer_callback(TimerHandle_t xTimer);
static bool npwt_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void npwt_adc_calibration_deinit(adc_cali_handle_t handle);

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
    // Q = 0.01 (过程噪声，适中的过程变化)
    // R = 0.1 (测量噪声协方差)
    // Q/R = 0.1 (平衡响应速度和稳定性)
    // initial_estimate = 0 (初始估计值，大气压开始)
    npwt_kalman_init(&g_npwt_system.pressure_kalman, 0.01f, 0.1f, 0.0f);

    // 初始化硬件
    ESP_ERROR_CHECK(npwt_adc_init());

    // 初始化I2C和PCA9685 (允许失败，以便没有硬件时也能运行)
    esp_err_t i2c_ret = npwt_pca9685_init();
    if (i2c_ret != ESP_OK) {
        ESP_LOGW(TAG, "PCA9685 initialization failed, PWM will use simulation mode: %s", esp_err_to_name(i2c_ret));
    }

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
        8192,  // 增加栈大小
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
    npwt_pwm_set_duty(4095);   // 100%占空比 = 泵停止
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

    // 任务心跳调试
    uint32_t heartbeat_count = 0;

    while (1) {
        heartbeat_count++;
        if (heartbeat_count % 20 == 0) {  // 每2秒输出一次心跳
            ESP_LOGI(TAG, "Control task heartbeat: %lu, power=%s",
                     heartbeat_count, g_npwt_system.settings.power_on ? "ON" : "OFF");
        }
        xSemaphoreTake(g_npwt_system.data_mutex, portMAX_DELAY);

        if (g_npwt_system.settings.power_on) {
            // 每100ms读取一次压力（10Hz，与控制循环同步）
            static uint32_t last_pressure_read = 0;
            uint32_t current_time = esp_timer_get_time() / 1000;
            if (current_time - last_pressure_read >= 100) {
                last_pressure_read = current_time;
                g_npwt_system.realtime.current_pressure = npwt_adc_read_pressure();
            }

            // 控制任务状态调试
            static uint32_t last_control_debug = 0;
            if (current_time - last_control_debug >= 2000) {
                last_control_debug = current_time;
                ESP_LOGI(TAG, "Control Task: power=ON, mode=%d, state=%d, pressure=%d",
                         g_npwt_system.settings.mode, g_npwt_system.realtime.state, g_npwt_system.realtime.current_pressure);
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
                npwt_pwm_set_duty(4095);   // 100%占空比 = 泵停止
            }
        } else {
            // 系统关闭，停止泵
            npwt_pwm_set_duty(4095);   // 100%占空比 = 泵停止
            g_npwt_system.realtime.state = NPWT_STATE_IDLE;
            // pump_pwm会在npwt_pwm_set_duty()中自动更新

            // 调试：确认系统关闭状态
            static uint32_t last_off_debug = 0;
            uint32_t current_time = esp_timer_get_time() / 1000;
            if (current_time - last_off_debug >= 2000) {
                last_off_debug = current_time;
                ESP_LOGI(TAG, "Control Task: power=OFF, state=IDLE, PWM=100%% (SYSTEM IS POWERED OFF!)");
            }
        }

        // 更新流量分析和密封性检测
        npwt_update_flow_analysis();

        // 更新密封检查
        npwt_seal_check();

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

    // === 旧传感器参数 (已保存) ===
    // 第二个传感器: 大气压800mV, 计算系数0.16
    // float relative_pressure_kpa = (voltage_mv - 800) * 0.16f;

    // === 第一个传感器参数 (当前使用) ===
    // 大气压时电压: 480mV, -98kPa时2500mV
    // 斜率 = -98kPa / (2500mV - 480mV) = -98 / 2020 = -0.0485 kPa/mV
    // 为了达到-100kPa范围，稍微调整系数: -0.0495 kPa/mV
    float relative_pressure_kpa = (voltage_mv - NPWT_ZERO_POINT_VOLTAGE) * (-0.0495f);

    // 使用卡尔曼滤波平滑压力值
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

    // 使用PCA9685输出PWM信号 (通过共享I2C)
    if (g_npwt_system.i2c_initialized) {
        esp_err_t ret = npwt_pca9685_set_pwm(NPWT_PCA9685_PWM_CHANNEL, duty);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set PCA9685 PWM: %s", esp_err_to_name(ret));
        }
    }

    g_npwt_system.realtime.pump_pwm = duty;

    ESP_LOGI(TAG, "PWM duty set to: %d (pump_enabled: %s)", duty, pump_enabled ? "YES" : "NO");
    return ESP_OK;
}

// PID控制器初始化
esp_err_t npwt_pid_init(npwt_pid_t *pid) {
    if (!pid) return ESP_ERR_INVALID_ARG;

    // PID参数 - 反向PWM控制（100%=停止，60%=最快）
    pid->kp = 10.0f;    // 降低比例系数，减少响应速度
    pid->ki = 0.5f;     // 降低积分系数，减少积分累积
    pid->kd = 0.2f;     // 降低微分系数，减少震荡

    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->output_min = 0.0f;         // PID输出最小值
    pid->output_max = 1638.0f;      // PID输出最大值 (40% of 4095)
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

    // 最小时间间隔保护，避免dt为0
    if (dt <= 0.001f) {
        dt = 0.001f;  // 最小1ms间隔
    }

    // 对于反向PWM控制：当前压力 - 目标压力 = 误差
    // 如果当前压力(0) > 目标压力(-30)，误差为正，需要减小PWM占空比
    float error = input - setpoint;

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

    // 调试日志 - 每2秒输出一次
    static uint32_t last_debug_time = 0;
    if (current_time - last_debug_time >= 2000) {
        last_debug_time = current_time;
        ESP_LOGI(TAG, "PID Debug: setpoint=%.1f, input=%.1f, error=%.1f, dt=%.3f, pid_out=%.1f",
                 setpoint, input, error, dt, output);
        ESP_LOGI(TAG, "PID Terms: P=%.1f, I=%.1f, D=%.1f",
                 pid->kp * error, pid->ki * pid->integral, pid->kd * derivative);
    }

    pid->prev_error = error;
    pid->last_time = current_time;

    return output;
}

// PWM测试模式重置函数（保留用于其他测试）
static uint32_t pwm_test_start_time = 0;

void npwt_reset_pwm_test_mode(void) {
    pwm_test_start_time = 0;
}

// 持续模式运行
esp_err_t npwt_mode_continuous_run(void) {
    g_npwt_system.realtime.state = NPWT_STATE_WORKING;

    // 添加调试信息
    static uint32_t last_debug_time = 0;
    uint32_t current_time = esp_timer_get_time() / 1000;

    if (current_time - last_debug_time >= 2000) {
        last_debug_time = current_time;
        ESP_LOGI(TAG, "Continuous Mode: target=%d, current=%d",
                 g_npwt_system.settings.target_pressure,
                 g_npwt_system.realtime.current_pressure);
    }

    // 使用PID控制器调节泵速
    float pid_output = npwt_pid_calculate(
        &g_npwt_system.pid,
        g_npwt_system.settings.target_pressure,
        g_npwt_system.realtime.current_pressure
    );

    // 反向PWM控制（100%占空比=泵停止，0%占空比=泵最快）
    uint16_t pwm_duty = 4095 - (uint16_t)pid_output;
    if (pwm_duty > 4095) pwm_duty = 4095;  // 最大100%（泵停止）
    if (pwm_duty < 2457) pwm_duty = 2457;  // 最小60%（泵最快，安全限制）

    ESP_LOGI(TAG, "PID output: %.2f, PWM duty: %d (%.1f%%)", pid_output, pwm_duty, (pwm_duty * 100.0f) / 4095.0f);
    npwt_pwm_set_duty(pwm_duty);

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
            npwt_pwm_set_duty(4095);   // 100%占空比 = 泵停止
        } else {
            // 继续工作
            float pid_output = npwt_pid_calculate(
                &g_npwt_system.pid,
                g_npwt_system.settings.target_pressure,
                g_npwt_system.realtime.current_pressure
            );
            // 反向PWM控制
            uint16_t pwm_duty = 4095 - (uint16_t)pid_output;
            if (pwm_duty > 4095) pwm_duty = 4095;
            if (pwm_duty < 2457) pwm_duty = 2457;
            npwt_pwm_set_duty(pwm_duty);
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
            npwt_pwm_set_duty(4095);   // 100%占空比 = 泵停止
        }
    } else {
        // 初始状态，开始工作 (只在第一次进入时重置PID)
        if (g_npwt_system.realtime.state == NPWT_STATE_IDLE) {
            g_npwt_system.realtime.state = NPWT_STATE_WORKING;
            g_npwt_system.realtime.work_elapsed = 0;
            g_npwt_system.realtime.rest_elapsed = 0;
            npwt_pid_reset(&g_npwt_system.pid);
            ESP_LOGI(TAG, "Intermittent mode started, PID reset");
        }
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
            // 反向PWM控制
            uint16_t pwm_duty = 4095 - (uint16_t)pid_output;
            if (pwm_duty > 4095) pwm_duty = 4095;
            if (pwm_duty < 2457) pwm_duty = 2457;
            npwt_pwm_set_duty(pwm_duty);
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
            // 反向PWM控制
            uint16_t pwm_duty = 4095 - (uint16_t)pid_output;
            if (pwm_duty > 4095) pwm_duty = 4095;
            if (pwm_duty < 2457) pwm_duty = 2457;
            npwt_pwm_set_duty(pwm_duty);
        }
    } else {
        // 初始状态，开始工作 (只在第一次进入时重置PID)
        if (g_npwt_system.realtime.state == NPWT_STATE_IDLE) {
            g_npwt_system.realtime.state = NPWT_STATE_WORKING;
            g_npwt_system.realtime.work_elapsed = 0;
            g_npwt_system.realtime.rest_elapsed = 0;
            npwt_pid_reset(&g_npwt_system.pid);
            ESP_LOGI(TAG, "Dynamic mode started, PID reset");
        }
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

    // 检查密封质量 - 临时禁用用于PWM测试
    if (g_npwt_system.realtime.seal_quality < 50) {
        ESP_LOGW(TAG, "Poor seal quality: %d%%", g_npwt_system.realtime.seal_quality);
        // return false;  // 临时禁用，允许PWM测试
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
        // 关闭时：停止泵，重置状态
        g_npwt_system.realtime.state = NPWT_STATE_IDLE;
        g_npwt_system.realtime.work_elapsed = 0;
        g_npwt_system.realtime.rest_elapsed = 0;
        g_npwt_system.realtime.pump_pwm = 4095;  // 100%占空比 = 泵停止
        npwt_pid_reset(&g_npwt_system.pid);
    } else {
        // 开启时：重置状态和计时器
        g_npwt_system.realtime.state = NPWT_STATE_IDLE;
        g_npwt_system.realtime.work_elapsed = 0;
        g_npwt_system.realtime.rest_elapsed = 0;
        g_npwt_system.realtime.pump_pwm = 0;  // 初始为0，等待PID控制
        npwt_reset_pwm_test_mode();  // 重置PWM测试模式
        ESP_LOGI(TAG, "System started, PID control will begin");
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

    // 清理I2C资源
    if (g_npwt_system.i2c_initialized) {
        npwt_i2c_deinit();
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

// 流量计算相关函数实现

// 根据基准性能曲线计算理论流量（100%功率下）
float npwt_calculate_theoretical_flow(float pressure_kpa) {
    float abs_pressure = fabsf(pressure_kpa);

    if (abs_pressure >= 77.0f) {
        return 0.0f;  // 超过最大真空
    } else if (abs_pressure >= 5.0f) {
        // 段1: -77kPa到-5kPa, 流量0-11L/min
        // 线性插值: flow = 0 + (11-0) * (77-abs_pressure)/(77-5)
        return 11.0f * (77.0f - abs_pressure) / 72.0f;
    } else {
        // 段2: -5kPa到0kPa, 流量11-15L/min
        // 线性插值: flow = 11 + (15-11) * (5-abs_pressure)/(5-0)
        return 11.0f + 4.0f * (5.0f - abs_pressure) / 5.0f;
    }
}

// 根据压力和PWM计算实际流量
float npwt_calculate_actual_flow(float pressure_kpa, uint16_t pwm_duty) {
    // 1. 从基准曲线(100%功率)查找理论最大流量
    float max_flow_at_pressure = npwt_calculate_theoretical_flow(pressure_kpa);

    // 2. 根据PWM占空比线性缩放
    float pwm_ratio = (float)pwm_duty / 4095.0f;
    float actual_flow = max_flow_at_pressure * pwm_ratio;

    return actual_flow;
}

// 流量转换为UI进度条百分比
uint8_t npwt_flow_to_bar_percentage(float flow_lpm) {
    // 0% = 0L/min, 100% = 15L/min (更直观的显示方式)
    uint8_t percentage = (uint8_t)(flow_lpm / 15.0f * 100.0f);
    return percentage > 100 ? 100 : percentage;
}

// 更新流量分析和密封性检测
void npwt_update_flow_analysis(void) {
    float current_pressure = (float)g_npwt_system.realtime.current_pressure;
    uint16_t pwm_duty = g_npwt_system.realtime.pump_pwm;

    // 计算实际流量
    float actual_flow = npwt_calculate_actual_flow(current_pressure, pwm_duty);
    g_npwt_system.realtime.actual_flow = actual_flow;

    // 计算漏气流量（在稳态时，实际流量主要用于补偿漏气）
    // 假设正常密封时需要0.5L/min维持压力
    float normal_maintenance_flow = 0.5f;
    g_npwt_system.realtime.leakage_flow = fmaxf(0.0f, actual_flow - normal_maintenance_flow);

    // 基于漏气流量更新密封质量
    float leakage = g_npwt_system.realtime.leakage_flow;
    if (leakage < 1.0f) {
        g_npwt_system.realtime.seal_quality = 100;  // 优秀
    } else if (leakage < 3.0f) {
        g_npwt_system.realtime.seal_quality = 80;   // 良好
    } else if (leakage < 6.0f) {
        g_npwt_system.realtime.seal_quality = 60;   // 一般
    } else if (leakage < 10.0f) {
        g_npwt_system.realtime.seal_quality = 40;   // 较差
    } else {
        g_npwt_system.realtime.seal_quality = 20;   // 很差
    }

    ESP_LOGD(TAG, "Flow analysis - Actual: %.1f L/min, Leakage: %.1f L/min, Seal: %d%%",
             actual_flow, leakage, g_npwt_system.realtime.seal_quality);
}

// I2C和PCA9685控制功能实现

// 初始化I2C总线 - 使用触摸屏已初始化的I2C总线
esp_err_t npwt_i2c_init(void) {
    if (g_npwt_system.i2c_initialized) {
        return ESP_OK; // 已经初始化
    }

    // 注意：I2C总线已由触摸屏系统初始化，这里只是标记为已初始化
    // 实际的I2C通信将使用触摸屏的I2C_NUM_0端口
    g_npwt_system.i2c_initialized = true;
    ESP_LOGI(TAG, "Using shared I2C bus (SDA:%d, SCL:%d)", NPWT_I2C_SDA_GPIO, NPWT_I2C_SCL_GPIO);
    return ESP_OK;
}

// 初始化PCA9685 - 使用共享I2C总线
esp_err_t npwt_pca9685_init(void) {
    esp_err_t ret;

    // 确保I2C已初始化
    ret = npwt_i2c_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // 使用共享的I2C端口直接通信

    // Step 1: 复位PCA9685 (参考Arduino代码)
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){0x00, 0x00}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCA9685 reset failed (device might not be connected): %s", esp_err_to_name(ret));
        // 继续运行，只是没有硬件PWM控制
        return ESP_OK; // 不返回错误，允许程序继续运行
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // 等待复位完成

    // Step 2: 设置PWM频率为20kHz (静音运行)
    uint16_t pwm_frequency = 20000;
    ret = npwt_pca9685_set_frequency(pwm_frequency);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set PWM frequency: %s", esp_err_to_name(ret));
    }

    // Step 3: 设置MODE2寄存器 (参考Arduino代码)
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){0x01, 0x04}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set MODE2: %s", esp_err_to_name(ret));
    }

    // Step 4: 设置MODE1寄存器启用自动增量 (参考Arduino代码)
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){0x00, 0x80}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set MODE1: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "PCA9685 initialized using shared I2C (addr: 0x%02X)", NPWT_PCA9685_ADDR);
    return ESP_OK;
}

// 设置PCA9685的PWM输出
esp_err_t npwt_pca9685_set_pwm(uint8_t channel, uint16_t duty) {
    if (channel > 15) {
        ESP_LOGE(TAG, "Invalid PWM channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    if (duty > 4095) {
        duty = 4095; // 限制最大值
    }

    // PCA9685每个通道有4个寄存器：LEDn_ON_L, LEDn_ON_H, LEDn_OFF_L, LEDn_OFF_H
    uint8_t reg_base = 0x06 + 4 * channel; // 通道寄存器基地址
    uint16_t on_value = 0;     // 从0开始 (参考Arduino代码)
    uint16_t off_value = duty; // 占空比值 (参考Arduino代码)

    // 分别写入4个寄存器 (参考Arduino代码逻辑)
    esp_err_t ret;

    // 写入LEDn_ON_L
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){reg_base, on_value & 0xFF}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) return ret;

    // 写入LEDn_ON_H
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){reg_base + 1, on_value >> 8}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) return ret;

    // 写入LEDn_OFF_L
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){reg_base + 2, off_value & 0xFF}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) return ret;

    // 写入LEDn_OFF_H
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){reg_base + 3, off_value >> 8}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set PWM channel %d (device might not be connected): %s", channel, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "PWM channel %d set to duty: %d (0x%03X)", channel, duty, duty);
    return ESP_OK;
}

// 动态设置PCA9685频率
esp_err_t npwt_pca9685_set_frequency(uint16_t frequency_hz) {
    if (!g_npwt_system.i2c_initialized) {
        ESP_LOGE(TAG, "I2C not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    // 限制频率范围：40Hz - 20000Hz (prescale 0-152)
    if (frequency_hz < 40 || frequency_hz > 20000) {
        ESP_LOGE(TAG, "Frequency %d Hz out of range (40-20000 Hz)", frequency_hz);
        return ESP_ERR_INVALID_ARG;
    }

    // 计算prescale值：PRE_SCALE = round(25MHz / (4096 * freq)) - 1
    uint8_t prescale = (uint8_t)(25000000.0f / (4096.0f * frequency_hz) - 1 + 0.5f);

    ESP_LOGI(TAG, "Setting PWM frequency to %d Hz (prescale=%d)", frequency_hz, prescale);

    // 参考Arduino代码的频率设置逻辑
    // 1. 读取当前MODE1寄存器值
    uint8_t read_cmd = 0x00;
    uint8_t oldmode;
    esp_err_t ret = i2c_master_write_read_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                                 &read_cmd, 1, &oldmode, 1,
                                                 NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read MODE1: %s", esp_err_to_name(ret));
        oldmode = 0x00; // 使用默认值
    }

    // 2. 进入睡眠模式
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){0x00, (oldmode & 0x7F) | 0x10}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enter sleep mode: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. 设置预分频寄存器
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){0xFE, prescale}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set prescale: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. 恢复原模式
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){0x00, oldmode}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore MODE1: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(5)); // 等待稳定

    // 5. 启用自动增量
    ret = i2c_master_write_to_device(I2C_NUM_0, NPWT_PCA9685_ADDR,
                                     (uint8_t[]){0x00, oldmode | 0x80}, 2,
                                     NPWT_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable auto-increment: %s", esp_err_to_name(ret));
        return ret;
    }

    // 计算实际频率并显示
    float actual_freq = 25000000.0f / (4096.0f * (prescale + 1));
    ESP_LOGI(TAG, "PWM frequency set. Target: %d Hz, Actual: %.1f Hz", frequency_hz, actual_freq);

    return ESP_OK;
}

// PCA9685测试输出函数
esp_err_t npwt_pca9685_test_output(void) {
    ESP_LOGI(TAG, "=== PCA9685 Test Output ===");
    ESP_LOGI(TAG, "I2C Address: 0x%02X, Channel: %d", NPWT_PCA9685_ADDR, NPWT_PCA9685_PWM_CHANNEL);

    if (!g_npwt_system.i2c_initialized) {
        ESP_LOGE(TAG, "I2C not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    // 测试不同的PWM值
    uint16_t test_values[] = {0, 1024, 2048, 4095}; // 0%, 25%, 50%, 100%
    const char* test_names[] = {"0%", "25%", "50%", "100%"};

    for (int i = 0; i < 4; i++) {
        ESP_LOGI(TAG, "Testing PWM %s (%d)...", test_names[i], test_values[i]);
        esp_err_t ret = npwt_pca9685_set_pwm(NPWT_PCA9685_PWM_CHANNEL, test_values[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Test failed at %s: %s", test_names[i], esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // 等待2秒，便于观察
    }

    ESP_LOGI(TAG, "=== Test Complete ===");
    return ESP_OK;
}

// I2C设备扫描功能
esp_err_t npwt_i2c_scan_devices(void) {
    ESP_LOGI(TAG, "=== I2C Device Scan ===");

    if (!g_npwt_system.i2c_initialized) {
        ESP_LOGE(TAG, "I2C not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    int devices_found = 0;

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        // 尝试写入一个字节到设备
        esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, addr,
                                                   (uint8_t[]){0x00}, 1,
                                                   50 / portTICK_PERIOD_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address 0x%02X", addr);
            devices_found++;

            // 特殊标记PCA9685
            if (addr == NPWT_PCA9685_ADDR) {
                ESP_LOGI(TAG, "  ^-- This is our PCA9685!");
            }
        }
    }

    ESP_LOGI(TAG, "I2C scan complete. Found %d devices.", devices_found);

    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found! Check connections.");
    }

    return ESP_OK;
}

// 释放I2C资源
esp_err_t npwt_i2c_deinit(void) {
    if (!g_npwt_system.i2c_initialized) {
        return ESP_OK; // 未初始化
    }

    // 共享I2C总线，不需要释放硬件资源
    // 只标记为未初始化
    g_npwt_system.i2c_initialized = false;
    ESP_LOGI(TAG, "NPWT I2C access disabled (shared bus remains active)");
    return ESP_OK;
}
