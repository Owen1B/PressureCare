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
void npwt_ui_update_seal_quality(uint8_t quality);
void npwt_ui_update_power_button(bool power_on);
void npwt_ui_update_mode_display(npwt_mode_t mode);
void npwt_ui_update_state_display(npwt_state_t state);

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
const char* npwt_ui_get_state_string(npwt_state_t state);
void npwt_ui_format_pressure(int16_t pressure, char* buffer, size_t buffer_size);
void npwt_ui_format_time(uint8_t minutes, char* buffer, size_t buffer_size);

// 临时设置变量 (用于设置界面)
extern npwt_settings_t temp_settings;
extern bool settings_modified;

#ifdef __cplusplus
}
#endif

#endif // NPWT_UI_BRIDGE_H