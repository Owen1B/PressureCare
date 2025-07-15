idf_component_register(
    SRCS "waveshare_rgb_lcd_port.c" "main.c" "lvgl_port.c" "npwt_core.c" "npwt_ui_bridge.c" "npwt_ui_events.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl__lvgl squareline_ui nvs_flash driver)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
