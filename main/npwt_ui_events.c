#include "npwt_ui_bridge.h"
#include "ui.h"
#include "lvgl_port.h"

static const char *TAG = "NPWT_UI_EVENTS";

// 电源按钮事件处理
void npwt_ui_event_power_button(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_power_button_clicked();
            lvgl_port_unlock();
        }
    }
}

// 设置按钮事件处理
void npwt_ui_event_settings_button(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_settings_button_clicked();
            lvgl_port_unlock();
        }
    }
}

// 主页按钮事件处理
void npwt_ui_event_home_button(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_home_button_clicked();
            lvgl_port_unlock();
        }
    }
}

// 压力调节按钮事件处理
void npwt_ui_event_pressure_up(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_pressure_adjust(NPWT_PRESSURE_STEP);
            lvgl_port_unlock();
        }
    }
}

void npwt_ui_event_pressure_down(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_pressure_adjust(-NPWT_PRESSURE_STEP);
            lvgl_port_unlock();
        }
    }
}

// 工作时间调节按钮事件处理
void npwt_ui_event_work_time_up(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_work_time_adjust(1);
            lvgl_port_unlock();
        }
    }
}

void npwt_ui_event_work_time_down(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_work_time_adjust(-1);
            lvgl_port_unlock();
        }
    }
}

// 休息时间调节按钮事件处理
void npwt_ui_event_rest_time_up(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_rest_time_adjust(1);
            lvgl_port_unlock();
        }
    }
}

void npwt_ui_event_rest_time_down(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_rest_time_adjust(-1);
            lvgl_port_unlock();
        }
    }
}

// 模式切换按钮事件处理
void npwt_ui_event_mode_switch(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_mode_switch();
            lvgl_port_unlock();
        }
    }
}

// 保存设置按钮事件处理
void npwt_ui_event_save_settings(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (lvgl_port_lock(100)) {
            npwt_ui_handle_save_settings();
            lvgl_port_unlock();
        }
    }
}

// 注册所有UI事件处理器
void npwt_ui_register_events(void) {
    ESP_LOGI(TAG, "Registering UI event handlers...");
    
    // 这里需要根据实际的UI组件来注册事件处理器
    // 由于当前的UI组件名称可能不完全对应，需要在实际UI文件中查找正确的组件名称
    
    // 示例：
    // lv_obj_add_event_cb(ui_BTN_Power, npwt_ui_event_power_button, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(ui_BTN_Settings, npwt_ui_event_settings_button, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(ui_BTN_Home, npwt_ui_event_home_button, LV_EVENT_CLICKED, NULL);
    
    ESP_LOGI(TAG, "UI event handlers registered successfully");
}