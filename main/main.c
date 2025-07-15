/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "ui.h"
#include "npwt_core.h"
#include "npwt_ui_bridge.h"

void app_main()
{
    ESP_LOGI(TAG, "Starting NPWT Therapy Device...");
    
    // Initialize hardware
    waveshare_esp32_s3_rgb_lcd_init(); // Initialize the Waveshare ESP32-S3 RGB LCD 
    // wavesahre_rgb_lcd_bl_on();  //Turn on the screen backlight 
    // wavesahre_rgb_lcd_bl_off(); //Turn off the screen backlight 
    
    // Initialize NPWT core system
    ESP_ERROR_CHECK(npwt_system_init());
    
    ESP_LOGI(TAG, "Display NPWT Therapy Device UI");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        // Initialize and display NPWT therapy device UI
        ui_init();
        
        // Initialize UI bridge
        npwt_ui_bridge_init();
        
        // Note: LVGL demos have been disabled to save flash space
        // Only NPWT therapy device UI is available
        
        // Release the mutex
        lvgl_port_unlock();
    }
    
    // Start NPWT system
    ESP_ERROR_CHECK(npwt_system_start());
    
    ESP_LOGI(TAG, "NPWT Therapy Device started successfully");
}
