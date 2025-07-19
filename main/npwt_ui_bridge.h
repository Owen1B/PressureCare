#ifndef NPWT_UI_BRIDGE_H
#define NPWT_UI_BRIDGE_H

#include "npwt_core.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// UI桥接器初始化
esp_err_t npwt_ui_bridge_init(void);

// UI更新回调函数
void npwt_ui_update_callback(void);
void npwt_ui_force_update(void);

// 界面1 - 主治疗界面相关函数
void npwt_ui_update_main_screen(void);
void npwt_ui_update_pressure_display(int16_t current_pressure);
void npwt_ui_update_settings_panel(void);
void npwt_ui_set_bar1(uint8_t value);

// 界面2 - 设置界面相关函数
void npwt_ui_update_settings_screen(void);
void npwt_ui_update_pressure_setting(int16_t pressure);
void npwt_ui_update_work_time_setting(uint8_t minutes);
void npwt_ui_update_rest_time_setting(uint8_t minutes);
void npwt_ui_update_mode_setting(npwt_mode_t mode);

// 事件处理函数
void npwt_ui_handle_power_button_clicked(void);
void npwt_ui_handle_settings_button_clicked(void);
void npwt_ui_handle_home_button_clicked(void);
void npwt_ui_handle_pressure_adjust(int16_t delta);
void npwt_ui_handle_work_time_adjust(int8_t delta);
void npwt_ui_handle_rest_time_adjust(int8_t delta);
void npwt_ui_handle_mode_switch(void);
void npwt_ui_handle_save_settings(void);

// 参数验证和格式化
bool npwt_ui_validate_pressure(int16_t pressure);
bool npwt_ui_validate_time(uint8_t minutes);
const char* npwt_ui_get_mode_string(npwt_mode_t mode);
void npwt_ui_format_pressure(int16_t pressure, char* buffer, size_t buffer_size);
void npwt_ui_format_time(uint8_t minutes, char* buffer, size_t buffer_size);

// 系统状态相关
typedef enum {
    NPWT_SYSTEM_STATUS_INIT = 0,      // 系统初始化中
    NPWT_SYSTEM_STATUS_READY,         // 准备就绪
    NPWT_SYSTEM_STATUS_RUNNING,       // 正常运行中
    NPWT_SYSTEM_STATUS_I2C_ERROR,     // I2C异常
    NPWT_SYSTEM_STATUS_PCA9685_ERROR, // PCA9685异常
    NPWT_SYSTEM_STATUS_LEAK,          // 敷料漏气
    NPWT_SYSTEM_STATUS_BLOCKAGE,      // 管道堵塞
} npwt_system_status_t;

// 异常检测相关
typedef struct {
    uint32_t leak_detection_start;    // 漏气检测开始时间(ms)
    uint32_t blockage_detection_start; // 堵塞检测开始时间(ms)
    bool auto_stop_enabled;           // 是否开启异常自动停止
    npwt_system_status_t current_status; // 当前系统状态
} npwt_anomaly_detection_t;

// 全局异常检测状态
extern npwt_anomaly_detection_t g_anomaly_detection;

// 周期进度相关函数
uint8_t npwt_ui_get_cycle_progress(void);
void npwt_ui_update_cycle_progress(void);

// 系统状态相关函数
const char* npwt_ui_get_status_string(npwt_system_status_t status);
lv_color_t npwt_ui_get_status_color(npwt_system_status_t status);
void npwt_ui_update_system_status(void);
void npwt_ui_set_system_status(npwt_system_status_t status);

// 异常检测函数
void npwt_ui_check_anomalies(void);
void npwt_ui_handle_auto_stop_button_clicked(void);
bool npwt_ui_get_auto_stop_enabled(void);
void npwt_ui_set_auto_stop_enabled(bool enabled);
void npwt_ui_anomaly_detection_init(void);
void npwt_ui_update_auto_stop_button(void);

// 临时设置变量 (用于设置界面)
extern npwt_settings_t temp_settings;
extern bool settings_modified;

#ifdef __cplusplus
}
#endif

#endif // NPWT_UI_BRIDGE_H