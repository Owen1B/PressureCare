#include "npwt_ui_bridge.h"
#include "ui.h"
#include "lvgl_port.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"

static const char *TAG = "NPWT_UI_BRIDGE";

// 临时设置变量 (用于设置界面)
npwt_settings_t temp_settings;
bool settings_modified = false;

// 全局异常检测状态
npwt_anomaly_detection_t g_anomaly_detection = {
    .system_start_time = 0,
    .leak_detection_start = 0,
    .blockage_detection_start = 0,
    .auto_stop_enabled = false,
    .current_status = NPWT_SYSTEM_STATUS_INIT
};

// UI桥接器初始化
esp_err_t npwt_ui_bridge_init(void) {
    ESP_LOGI(TAG, "Initializing UI bridge...");

    // 注册UI更新回调
    npwt_register_ui_callback(npwt_ui_update_callback);

    // 初始化临时设置
    temp_settings = npwt_get_settings();

    // 初始化UI显示
    npwt_ui_update_main_screen();
    npwt_ui_update_settings_screen();

    // 模拟一次密封检查

    // 启动时扫描I2C设备
    ESP_LOGI(TAG, "Scanning I2C devices...");
    npwt_i2c_scan_devices();

    // 启动时测试PCA9685输出
    ESP_LOGI(TAG, "Running PCA9685 output test...");
    npwt_pca9685_test_output();

    // 初始化异常检测
    npwt_ui_anomaly_detection_init();

    ESP_LOGI(TAG, "UI bridge initialized successfully");
    return ESP_OK;
}

// UI更新回调函数
static bool force_ui_update = false;

void npwt_ui_force_update(void) {
    force_ui_update = true;
}

void npwt_ui_update_callback(void) {
    static uint32_t last_ui_update = 0;
    uint32_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒

    // 每1秒更新一次UI显示，或者强制更新
    if (force_ui_update || (current_time - last_ui_update >= 1000)) {
        last_ui_update = current_time;
        force_ui_update = false;

        if (lvgl_port_lock(10)) {
            // 更新主界面
            npwt_ui_update_main_screen();

            // 更新设置界面
            npwt_ui_update_settings_screen();

            lvgl_port_unlock();
        }
    }
}

// 更新主治疗界面
void npwt_ui_update_main_screen(void) {
    npwt_realtime_t realtime = npwt_get_realtime_data();
    npwt_settings_t settings = npwt_get_settings();

    // 更新当前压力显示
    npwt_ui_update_pressure_display(realtime.current_pressure);

    // 更新设置面板
    npwt_ui_update_settings_panel();


    // 更新周期进度条
    npwt_ui_update_cycle_progress();

    // 更新系统状态
    npwt_ui_update_system_status();

    // 检测异常
    npwt_ui_check_anomalies();

    // 更新泵速显示 (泵速 = 100% - PWM占空比)
    uint8_t pwm_percentage = (uint8_t)((realtime.pump_pwm * 100) / 4095);
    uint8_t pump_speed = 100 - pwm_percentage;
    char pump_speed_str[16];
    snprintf(pump_speed_str, sizeof(pump_speed_str), "%d%%", pump_speed);
    if (ui_Label_Head_Temp1) {
        lv_label_set_text(ui_Label_Head_Temp1, pump_speed_str);
    }



}

// 更新压力显示
void npwt_ui_update_pressure_display(int16_t current_pressure) {
    char pressure_str[32];

    // 直接显示所有压力值
    snprintf(pressure_str, sizeof(pressure_str), "%d", current_pressure);

    // 更新大号压力显示 (使用Label1作为当前压力数值显示)
    if (ui_Label1) {
        lv_label_set_text(ui_Label1, pressure_str);
    }
}

// 更新设置面板
void npwt_ui_update_settings_panel(void) {
    // 优先使用临时设置（如果用户正在修改设置），否则使用系统设置
    npwt_settings_t settings = settings_modified ? temp_settings : npwt_get_settings();

    // 更新当前目标负压 (ui_Label_Head_Temp2显示当前目标压力值，间歇模式下会变化)
    char pressure_str[16];
    int16_t current_target = npwt_get_current_target_pressure();
    npwt_ui_format_pressure(current_target, pressure_str, sizeof(pressure_str));
    if (ui_Label_Head_Temp2) {
        lv_label_set_text(ui_Label_Head_Temp2, pressure_str);
    }

    // 更新工作模式显示 (ui_Label_Bed_Temp2显示工作模式)
    const char* mode_str = npwt_ui_get_mode_string(settings.mode);
    if (ui_Label_Bed_Temp2) {
        lv_label_set_text(ui_Label_Bed_Temp2, mode_str);
    }
}

// 设置bar1
void npwt_ui_set_bar1(uint8_t value) {
    // 更新PWM占空比进度条 (ui_Bar1用于显示PWM占空比百分比)
    if (ui_Bar1) {
        lv_bar_set_value(ui_Bar1, value, LV_ANIM_ON);
    }
}


// 更新电源按钮状态

// 更新设置界面
void npwt_ui_update_settings_screen(void) {
    // 使用临时设置来更新设置界面
    npwt_ui_update_pressure_setting(temp_settings.target_pressure);
    npwt_ui_update_work_time_setting(temp_settings.work_time);
    npwt_ui_update_rest_time_setting(temp_settings.rest_time);
    npwt_ui_update_mode_setting(temp_settings.mode);

    // 更新异常自动停止按钮状态
    npwt_ui_update_auto_stop_button();
}

// 更新压力设置
void npwt_ui_update_pressure_setting(int16_t pressure) {
    char pressure_str[16];
    npwt_ui_format_pressure(pressure, pressure_str, sizeof(pressure_str));

    // 更新压力设置显示 (ui_Label_Z_Position_Number1显示设定压力)
    if (ui_Label_Z_Position_Number1) {
        lv_label_set_text(ui_Label_Z_Position_Number1, pressure_str);
    }
}

// 更新工作时间设置
void npwt_ui_update_work_time_setting(uint8_t minutes) {
    char time_str[16];
    npwt_ui_format_time(minutes, time_str, sizeof(time_str));

    // 更新工作时间显示 (ui_Label_X_Position_Number2显示工作时间)
    if (ui_Label_X_Position_Number2) {
        lv_label_set_text(ui_Label_X_Position_Number2, time_str);
    }
}

// 更新休息时间设置
void npwt_ui_update_rest_time_setting(uint8_t minutes) {
    char time_str[16];
    npwt_ui_format_time(minutes, time_str, sizeof(time_str));

    // 更新休息时间显示 (ui_Label_Time_7显示休息时间)
    if (ui_Label_Time_7) {
        lv_label_set_text(ui_Label_Time_7, time_str);
    }
}

// 更新模式设置
void npwt_ui_update_mode_setting(npwt_mode_t mode) {
    // 更新模式滚轮选择 (ui_Roller7显示模式选择)
    if (ui_Roller7) {
        lv_roller_set_selected(ui_Roller7, mode, LV_ANIM_ON);
    }
}

// 事件处理函数
void npwt_ui_handle_power_button_clicked(void) {
    npwt_settings_t settings = npwt_get_settings();
    bool new_power_state = !settings.power_on;
    npwt_set_power(new_power_state);

    // 更新系统状态
    if (new_power_state) {
        // 开启电源时，设置为正常运行状态
        npwt_ui_set_system_status(NPWT_SYSTEM_STATUS_RUNNING);
    } else {
        // 手动关闭电源时，清除所有状态回到准备就绪
        npwt_ui_set_system_status(NPWT_SYSTEM_STATUS_READY);
        // 清除异常检测计时器
        g_anomaly_detection.leak_detection_start = 0;
        g_anomaly_detection.blockage_detection_start = 0;
    }

    ESP_LOGI(TAG, "Power button clicked: %s -> %s",
             settings.power_on ? "ON" : "OFF",
             new_power_state ? "ON" : "OFF");
}

void npwt_ui_handle_settings_button_clicked(void) {
    // 进入设置界面时，加载当前设置到临时变量
    temp_settings = npwt_get_settings();
    settings_modified = false;

    ESP_LOGI(TAG, "Settings button clicked");
}

void npwt_ui_handle_home_button_clicked(void) {
    // 返回主界面时，如果有未保存的修改，可以提示用户
    if (settings_modified) {
        ESP_LOGW(TAG, "Settings modified but not saved");
        // 这里可以添加提示逻辑
    }

    ESP_LOGI(TAG, "Home button clicked");
}

void npwt_ui_handle_pressure_adjust(int16_t delta) {
    // 对于负压，+ 按钮应该增加绝对值（更负），- 按钮应该减少绝对值（更正）
    // 所以需要反转delta的符号
    int16_t new_pressure = temp_settings.target_pressure - delta;

    if (npwt_ui_validate_pressure(new_pressure)) {
        temp_settings.target_pressure = new_pressure;
        settings_modified = true;
        npwt_ui_force_update(); // 强制立即更新UI

        ESP_LOGI(TAG, "Pressure adjusted to: %d kPa", new_pressure);
    } else {
        ESP_LOGW(TAG, "Invalid pressure value: %d kPa", new_pressure);
    }
}

void npwt_ui_handle_work_time_adjust(int8_t delta) {
    int16_t new_time = temp_settings.work_time + delta;

    if (npwt_ui_validate_time(new_time)) {
        temp_settings.work_time = new_time;
        settings_modified = true;
        npwt_ui_force_update(); // 强制立即更新UI

        ESP_LOGI(TAG, "Work time adjusted to: %d minutes", new_time);
    } else {
        ESP_LOGW(TAG, "Invalid work time value: %d minutes", new_time);
    }
}

void npwt_ui_handle_rest_time_adjust(int8_t delta) {
    int16_t new_time = temp_settings.rest_time + delta;

    if (npwt_ui_validate_time(new_time)) {
        temp_settings.rest_time = new_time;
        settings_modified = true;
        npwt_ui_force_update(); // 强制立即更新UI

        ESP_LOGI(TAG, "Rest time adjusted to: %d minutes", new_time);
    } else {
        ESP_LOGW(TAG, "Invalid rest time value: %d minutes", new_time);
    }
}

void npwt_ui_handle_mode_switch(void) {
    // 循环切换模式（只有持续模式和间歇模式）
    temp_settings.mode = (temp_settings.mode + 1) % 2;
    settings_modified = true;
    npwt_ui_force_update(); // 强制立即更新UI

    ESP_LOGI(TAG, "Mode switched to: %s", npwt_ui_get_mode_string(temp_settings.mode));
}

void npwt_ui_handle_save_settings(void) {
    esp_err_t err = npwt_settings_save(&temp_settings);

    if (err == ESP_OK) {
        // 应用设置到系统
        npwt_set_target_pressure(temp_settings.target_pressure);
        npwt_set_mode(temp_settings.mode);
        // 立即应用工作时间和休息时间设置
        npwt_set_work_time(temp_settings.work_time);
        npwt_set_rest_time(temp_settings.rest_time);

        settings_modified = false;
        ESP_LOGI(TAG, "Settings saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to save settings");
    }
}

// 参数验证函数
bool npwt_ui_validate_pressure(int16_t pressure) {
    return (pressure >= NPWT_PRESSURE_MIN && pressure <= NPWT_PRESSURE_MAX);
}

bool npwt_ui_validate_time(uint8_t minutes) {
    return (minutes >= NPWT_TIME_MIN && minutes <= NPWT_TIME_MAX);
}

// 格式化函数
const char* npwt_ui_get_mode_string(npwt_mode_t mode) {
    switch (mode) {
        case NPWT_MODE_CONTINUOUS:
            return "持续模式";
        case NPWT_MODE_INTERMITTENT:
            return "间歇模式";
        default:
            return "未知模式";
    }
}


void npwt_ui_format_pressure(int16_t pressure, char* buffer, size_t buffer_size) {
    if (pressure == 0) {
        snprintf(buffer, buffer_size, "0 kPa");
    } else {
        snprintf(buffer, buffer_size, "%d kPa", pressure);
    }
}

void npwt_ui_format_time(uint8_t minutes, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d min", minutes);
}

// 周期进度相关函数
uint8_t npwt_ui_get_cycle_progress(void) {
    npwt_realtime_t realtime = npwt_get_realtime_data();
    npwt_settings_t settings = npwt_get_settings();

    // 如果系统未启动，返回0%
    if (!settings.power_on) {
        return 0;
    }

    switch (settings.mode) {
        case NPWT_MODE_CONTINUOUS:
            // 持续模式一直保持100%
            return 100;

        case NPWT_MODE_INTERMITTENT: {
            uint32_t work_time_sec = settings.work_time * 60;
            uint32_t rest_time_sec = settings.rest_time * 60;
            uint32_t total_cycle_sec = work_time_sec + rest_time_sec;

            if (total_cycle_sec == 0) return 0;

            uint32_t current_elapsed;
            if (realtime.state == NPWT_STATE_WORKING) {
                current_elapsed = realtime.work_elapsed;
            } else if (realtime.state == NPWT_STATE_RESTING) {
                current_elapsed = work_time_sec + realtime.rest_elapsed;
            } else {
                return 0;
            }

            // 计算周期进度百分比，使用浮点数计算提高精度
            float progress_float = ((float)current_elapsed / (float)total_cycle_sec) * 100.0f;
            uint8_t progress = (uint8_t)(progress_float + 0.5f); // 四舍五入
            return progress > 100 ? 100 : progress;
        }

        default:
            return 0;
    }
}

void npwt_ui_update_cycle_progress(void) {
    uint8_t progress = npwt_ui_get_cycle_progress();
    npwt_ui_set_bar1(progress);
}

// 系统状态相关函数
const char* npwt_ui_get_status_string(npwt_system_status_t status) {
    switch (status) {
        case NPWT_SYSTEM_STATUS_INIT:
            return "正常运行（初始化）";
        case NPWT_SYSTEM_STATUS_READY:
            return "准备就绪";
        case NPWT_SYSTEM_STATUS_RUNNING:
            return "正常运行";
        case NPWT_SYSTEM_STATUS_I2C_ERROR:
            return "I2C异常";
        case NPWT_SYSTEM_STATUS_PCA9685_ERROR:
            return "PCA9685异常";
        case NPWT_SYSTEM_STATUS_LEAK:
            return "敷料漏气";
        case NPWT_SYSTEM_STATUS_BLOCKAGE:
            return "管道堵塞";
        default:
            return "未知状态";
    }
}

lv_color_t npwt_ui_get_status_color(npwt_system_status_t status) {
    switch (status) {
        case NPWT_SYSTEM_STATUS_INIT:
        case NPWT_SYSTEM_STATUS_READY:
            return lv_color_hex(0xFFFB2C); // 黄色
        case NPWT_SYSTEM_STATUS_RUNNING:
            return lv_color_hex(0x00FF00); // 绿色
        case NPWT_SYSTEM_STATUS_I2C_ERROR:
        case NPWT_SYSTEM_STATUS_PCA9685_ERROR:
        case NPWT_SYSTEM_STATUS_LEAK:
        case NPWT_SYSTEM_STATUS_BLOCKAGE:
            return lv_color_hex(0xFF0000); // 红色
        default:
            return lv_color_hex(0xFFFFFF); // 白色
    }
}

void npwt_ui_update_system_status(void) {
    if (ui_Label_Header1) {
        const char* status_str = npwt_ui_get_current_status_string();  // 使用新的倒计时函数
        lv_color_t color = npwt_ui_get_status_color(g_anomaly_detection.current_status);

        lv_label_set_text(ui_Label_Header1, status_str);
        lv_obj_set_style_text_color(ui_Label_Header1, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void npwt_ui_set_system_status(npwt_system_status_t status) {
    g_anomaly_detection.current_status = status;
    npwt_ui_force_update();
}

// 异常检测函数
void npwt_ui_check_anomalies(void) {
    npwt_realtime_t realtime = npwt_get_realtime_data();
    npwt_settings_t settings = npwt_get_settings();
    uint32_t current_time = esp_timer_get_time() / 1000; // 转换为ms

    // 只在系统运行时进行状态管理
    if (!settings.power_on) {
        g_anomaly_detection.leak_detection_start = 0;
        g_anomaly_detection.blockage_detection_start = 0;
        g_anomaly_detection.system_start_time = 0;

        // 如果不是异常状态，则设为准备就绪；如果是异常状态，保持异常显示
        if (g_anomaly_detection.current_status != NPWT_SYSTEM_STATUS_LEAK &&
            g_anomaly_detection.current_status != NPWT_SYSTEM_STATUS_BLOCKAGE) {
            g_anomaly_detection.current_status = NPWT_SYSTEM_STATUS_READY;
        }
        return;
    }

    // 检查是否在30秒初始化期内
    uint32_t time_since_start = current_time - g_anomaly_detection.system_start_time;
    if (time_since_start < 30000) { // 30秒
        // 仍在初始化期，设置状态为初始化
        g_anomaly_detection.current_status = NPWT_SYSTEM_STATUS_INIT;
        g_anomaly_detection.leak_detection_start = 0;
        g_anomaly_detection.blockage_detection_start = 0;
        return;
    } else {
        // 超过30秒，进入正常运行状态
        if (g_anomaly_detection.current_status == NPWT_SYSTEM_STATUS_INIT) {
            g_anomaly_detection.current_status = NPWT_SYSTEM_STATUS_RUNNING;
        }
    }

    // 只有在开启异常自动停止时才进行异常检测
    if (!g_anomaly_detection.auto_stop_enabled) {
        return;
    }

    // 漏气检测: 如果正常运行时，系统的负压一直维持在-5到0kPa，同时设定的负压小于-10kPa
    bool leak_condition = (realtime.current_pressure >= -5 && realtime.current_pressure <= 0) &&
                         (settings.target_pressure < -10);

    if (leak_condition) {
        if (g_anomaly_detection.leak_detection_start == 0) {
            g_anomaly_detection.leak_detection_start = current_time;
        } else if (current_time - g_anomaly_detection.leak_detection_start >= 5000) { // 5秒
            // 如果开启了自动停止，先关闭电源
            if (g_anomaly_detection.auto_stop_enabled) {
                npwt_set_power(false);
                // 取消主界面电源按钮的checked状态
                extern lv_obj_t *ui_BTN_Pause_Top1;
                if (ui_BTN_Pause_Top1) {
                    lv_obj_clear_state(ui_BTN_Pause_Top1, LV_STATE_CHECKED);
                }
                ESP_LOGW(TAG, "Leak detected - auto stopping pump");
            }

            // 设置漏气状态（这样即使停机后也能保持显示）
            npwt_ui_set_system_status(NPWT_SYSTEM_STATUS_LEAK);
        }
    } else {
        g_anomaly_detection.leak_detection_start = 0;
    }

    // 堵塞检测: 如果传感器读取的负压一直小于-30kPa
    bool blockage_condition = realtime.current_pressure < -30;

    if (blockage_condition) {
        if (g_anomaly_detection.blockage_detection_start == 0) {
            g_anomaly_detection.blockage_detection_start = current_time;
        } else if (current_time - g_anomaly_detection.blockage_detection_start >= 5000) { // 5秒
            // 如果开启了自动停止，先关闭电源
            if (g_anomaly_detection.auto_stop_enabled) {
                npwt_set_power(false);
                // 取消主界面电源按钮的checked状态
                extern lv_obj_t *ui_BTN_Pause_Top1;
                if (ui_BTN_Pause_Top1) {
                    lv_obj_clear_state(ui_BTN_Pause_Top1, LV_STATE_CHECKED);
                }
                ESP_LOGW(TAG, "Blockage detected - auto stopping pump");
            }

            // 设置堵塞状态（这样即使停机后也能保持显示）
            npwt_ui_set_system_status(NPWT_SYSTEM_STATUS_BLOCKAGE);
        }
    } else {
        g_anomaly_detection.blockage_detection_start = 0;
    }
}

void npwt_ui_handle_auto_stop_button_clicked(void) {
    g_anomaly_detection.auto_stop_enabled = !g_anomaly_detection.auto_stop_enabled;

    // 如果关闭了自动停止，且系统正在运行，则重置状态为正常运行
    if (!g_anomaly_detection.auto_stop_enabled) {
        npwt_settings_t settings = npwt_get_settings();
        if (settings.power_on && (g_anomaly_detection.current_status == NPWT_SYSTEM_STATUS_LEAK ||
                                 g_anomaly_detection.current_status == NPWT_SYSTEM_STATUS_BLOCKAGE)) {
            npwt_ui_set_system_status(NPWT_SYSTEM_STATUS_RUNNING);
        }
        // 清除异常检测计时器
        g_anomaly_detection.leak_detection_start = 0;
        g_anomaly_detection.blockage_detection_start = 0;
    }

    ESP_LOGI(TAG, "Auto-stop %s", g_anomaly_detection.auto_stop_enabled ? "enabled" : "disabled");
}

bool npwt_ui_get_auto_stop_enabled(void) {
    return g_anomaly_detection.auto_stop_enabled;
}

void npwt_ui_set_auto_stop_enabled(bool enabled) {
    g_anomaly_detection.auto_stop_enabled = enabled;
}

void npwt_ui_update_auto_stop_button(void) {
    extern lv_obj_t *ui_BTN_Reset2;
    if (ui_BTN_Reset2) {
        if (g_anomaly_detection.auto_stop_enabled) {
            lv_obj_add_state(ui_BTN_Reset2, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_BTN_Reset2, LV_STATE_CHECKED);
        }
    }
}

void npwt_ui_anomaly_detection_init(void) {
    g_anomaly_detection.leak_detection_start = 0;
    g_anomaly_detection.blockage_detection_start = 0;
    g_anomaly_detection.auto_stop_enabled = false;
    g_anomaly_detection.current_status = NPWT_SYSTEM_STATUS_INIT;

    // 检查系统初始化状态
    // TODO: 检查I2C和PCA9685状态，设置相应的状态
    g_anomaly_detection.system_start_time = 0;  // 确保初始化时启动时间为0
    g_anomaly_detection.current_status = NPWT_SYSTEM_STATUS_READY;
}

// 获取初始化剩余时间（秒）
int npwt_ui_get_init_remaining_time(void) {
    if (g_anomaly_detection.current_status != NPWT_SYSTEM_STATUS_INIT || g_anomaly_detection.system_start_time == 0) {
        return 0;  // 不在初始化状态或未启动
    }

    uint32_t current_time = esp_timer_get_time() / 1000; // 转换为ms
    uint32_t elapsed = current_time - g_anomaly_detection.system_start_time;

    if (elapsed >= 30000) {
        return 0;  // 已超过30秒
    }

    return (30000 - elapsed) / 1000;  // 返回剩余秒数
}

// 获取当前状态字符串（包含倒计时）
const char* npwt_ui_get_current_status_string(void) {
    static char status_buffer[32];  // 静态缓冲区存储状态字符串

    if (g_anomaly_detection.current_status == NPWT_SYSTEM_STATUS_INIT) {
        int remaining_time = npwt_ui_get_init_remaining_time();
        if (remaining_time > 0) {
            snprintf(status_buffer, sizeof(status_buffer), "初始化中（%d秒）", remaining_time);
        } else {
            strcpy(status_buffer, npwt_ui_get_status_string(g_anomaly_detection.current_status));
        }
    } else {
        strcpy(status_buffer, npwt_ui_get_status_string(g_anomaly_detection.current_status));
    }

    return status_buffer;
}

// 模式和状态显示更新
