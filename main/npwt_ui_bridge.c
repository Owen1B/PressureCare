#include "npwt_ui_bridge.h"
#include "ui.h"
#include "lvgl_port.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "NPWT_UI_BRIDGE";

// 临时设置变量 (用于设置界面)
static npwt_settings_t temp_settings;
static bool settings_modified = false;

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
    npwt_seal_check();
    
    ESP_LOGI(TAG, "UI bridge initialized successfully");
    return ESP_OK;
}

// UI更新回调函数
void npwt_ui_update_callback(void) {
    if (lvgl_port_lock(10)) {
        // 更新主界面
        npwt_ui_update_main_screen();
        
        // 更新设置界面
        npwt_ui_update_settings_screen();
        
        lvgl_port_unlock();
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
    
    // 更新密封质量
    npwt_ui_update_seal_quality(realtime.seal_quality);
    
    // 更新电源按钮状态
    npwt_ui_update_power_button(settings.power_on);
    
    // 更新模式显示
    npwt_ui_update_mode_display(settings.mode);
    
    // 更新状态显示
    npwt_ui_update_state_display(realtime.state);
}

// 更新压力显示
void npwt_ui_update_pressure_display(int16_t current_pressure) {
    char pressure_str[32];
    snprintf(pressure_str, sizeof(pressure_str), "%d", current_pressure);
    
    // 更新大号压力显示 (使用Label1作为当前压力数值显示)
    if (ui_Label1) {
        lv_label_set_text(ui_Label1, pressure_str);
    }
}

// 更新设置面板
void npwt_ui_update_settings_panel(void) {
    npwt_settings_t settings = npwt_get_settings();
    
    // 更新设定负压 (ui_Label_Head_Temp2显示设定压力值)
    char pressure_str[16];
    npwt_ui_format_pressure(settings.target_pressure, pressure_str, sizeof(pressure_str));
    if (ui_Label_Head_Temp2) {
        lv_label_set_text(ui_Label_Head_Temp2, pressure_str);
    }
    
    // 更新工作模式显示 (ui_Label_Bed_Temp2显示工作模式)
    const char* mode_str = npwt_ui_get_mode_string(settings.mode);
    if (ui_Label_Bed_Temp2) {
        lv_label_set_text(ui_Label_Bed_Temp2, mode_str);
    }
}

// 更新密封质量
void npwt_ui_update_seal_quality(uint8_t quality) {
    // 更新密封质量进度条 (ui_Bar1用于显示密封质量)
    if (ui_Bar1) {
        lv_bar_set_value(ui_Bar1, quality, LV_ANIM_ON);
    }
}

// 更新电源按钮状态
void npwt_ui_update_power_button(bool power_on) {
    // 更新电源按钮的视觉状态 (ui_BTN_Pause_Top1作为总开关)
    if (ui_BTN_Pause_Top1) {
        if (power_on) {
            // 开启时显示绿色
            lv_obj_set_style_bg_color(ui_BTN_Pause_Top1, lv_color_hex(0x00FF00), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ui_BTN_Pause_Top1, 200, LV_PART_MAIN);
        } else {
            // 关闭时显示红色
            lv_obj_set_style_bg_color(ui_BTN_Pause_Top1, lv_color_hex(0xFF0000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ui_BTN_Pause_Top1, 200, LV_PART_MAIN);
        }
    }
}

// 更新设置界面
void npwt_ui_update_settings_screen(void) {
    // 使用临时设置来更新设置界面
    npwt_ui_update_pressure_setting(temp_settings.target_pressure);
    npwt_ui_update_work_time_setting(temp_settings.work_time);
    npwt_ui_update_rest_time_setting(temp_settings.rest_time);
    npwt_ui_update_mode_setting(temp_settings.mode);
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
    npwt_set_power(!settings.power_on);
    
    ESP_LOGI(TAG, "Power button clicked: %s", settings.power_on ? "OFF" : "ON");
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
    int16_t new_pressure = temp_settings.target_pressure + delta;
    
    if (npwt_ui_validate_pressure(new_pressure)) {
        temp_settings.target_pressure = new_pressure;
        settings_modified = true;
        
        ESP_LOGI(TAG, "Pressure adjusted to: %d mmHg", new_pressure);
    } else {
        ESP_LOGW(TAG, "Invalid pressure value: %d mmHg", new_pressure);
    }
}

void npwt_ui_handle_work_time_adjust(int8_t delta) {
    int16_t new_time = temp_settings.work_time + delta;
    
    if (npwt_ui_validate_time(new_time)) {
        temp_settings.work_time = new_time;
        settings_modified = true;
        
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
        
        ESP_LOGI(TAG, "Rest time adjusted to: %d minutes", new_time);
    } else {
        ESP_LOGW(TAG, "Invalid rest time value: %d minutes", new_time);
    }
}

void npwt_ui_handle_mode_switch(void) {
    // 循环切换模式
    temp_settings.mode = (temp_settings.mode + 1) % 3;
    settings_modified = true;
    
    ESP_LOGI(TAG, "Mode switched to: %s", npwt_ui_get_mode_string(temp_settings.mode));
}

void npwt_ui_handle_save_settings(void) {
    esp_err_t err = npwt_settings_save(&temp_settings);
    
    if (err == ESP_OK) {
        // 应用设置到系统
        npwt_set_target_pressure(temp_settings.target_pressure);
        npwt_set_mode(temp_settings.mode);
        // 工作时间和休息时间在下次启动时生效
        
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
        case NPWT_MODE_DYNAMIC:
            return "动态模式";
        default:
            return "未知模式";
    }
}

const char* npwt_ui_get_state_string(npwt_state_t state) {
    switch (state) {
        case NPWT_STATE_IDLE:
            return "待机";
        case NPWT_STATE_WORKING:
            return "工作中";
        case NPWT_STATE_RESTING:
            return "休息中";
        case NPWT_STATE_SEALING_CHECK:
            return "密封检查";
        case NPWT_STATE_ERROR:
            return "错误";
        default:
            return "未知状态";
    }
}

void npwt_ui_format_pressure(int16_t pressure, char* buffer, size_t buffer_size) {
    if (pressure == 0) {
        snprintf(buffer, buffer_size, "0 mmHg");
    } else {
        snprintf(buffer, buffer_size, "%d mmHg", pressure);
    }
}

void npwt_ui_format_time(uint8_t minutes, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d min", minutes);
}

// 模式和状态显示更新
void npwt_ui_update_mode_display(npwt_mode_t mode) {
    const char* mode_str = npwt_ui_get_mode_string(mode);
    
    // 更新模式显示
    // 示例：lv_label_set_text(ui_Label_Current_Mode, mode_str);
}

void npwt_ui_update_state_display(npwt_state_t state) {
    const char* state_str = npwt_ui_get_state_string(state);
    
    // 更新状态显示
    // 示例：lv_label_set_text(ui_Label_Current_State, state_str);
    
    // 根据状态改变指示灯颜色
    switch (state) {
        case NPWT_STATE_IDLE:
            // 设置为灰色
            break;
        case NPWT_STATE_WORKING:
            // 设置为绿色
            break;
        case NPWT_STATE_RESTING:
            // 设置为蓝色
            break;
        case NPWT_STATE_ERROR:
            // 设置为红色
            break;
        default:
            break;
    }
}