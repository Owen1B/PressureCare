#include "main/npwt_core.h"
#include "main/npwt_ui_bridge.h"
#include "components/squareline_ui/ui.h"

// 简单的编译测试文件
int test_build(void) {
    // 测试核心功能
    npwt_settings_t settings = npwt_get_settings();
    
    // 测试UI桥接功能
    npwt_ui_validate_pressure(-120);
    npwt_ui_validate_time(5);
    
    // 测试UI格式化功能
    char buffer[32];
    npwt_ui_format_pressure(-120, buffer, sizeof(buffer));
    npwt_ui_format_time(5, buffer, sizeof(buffer));
    
    // 测试模式字符串
    const char* mode_str = npwt_ui_get_mode_string(NPWT_MODE_CONTINUOUS);
    const char* state_str = npwt_ui_get_state_string(NPWT_STATE_IDLE);
    
    return 0;
}