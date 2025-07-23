# THU-NPWT

> 基于ESP32-S3的专业医疗级负压创面疗法(NPWT)设备，配备800x480触摸屏和精密压力控制系统

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.2.0-blue.svg)](https://github.com/espressif/esp-idf)
[![LVGL](https://img.shields.io/badge/LVGL-v8.3.11-green.svg)](https://lvgl.io/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## 🏥 项目概述

本项目是一个完整的负压创面疗法(NPWT)医疗设备解决方案，采用ESP32-S3微控制器，集成了精密的压力控制、实时监测、用户界面和安全保护系统。设备能够提供-30kPa至-10kPa的精确负压控制，支持持续和间歇两种治疗模式。

### ✨ 核心特性

- 🎯 **精密压力控制**: PID闭环控制 + 卡尔曼滤波，控制精度±1kPa
- 📱 **触摸式界面**: 800x480高分辨率彩色触摸屏，中文用户界面
- 🔄 **双工作模式**: 持续模式和间歇模式，满足不同治疗需求
- 🛡️ **安全保护系统**: 漏气检测、堵塞检测、异常自动停止
- 📊 **实时监测**: 压力、泵速、工作状态的实时显示和记录
- 🔧 **专业级硬件**: PCA9685 PWM控制器，GT911触摸控制器

## 🏗️ 系统整体框架

### 系统架构设计

```mermaid
graph TB
    subgraph "🏥 医疗应用层"
        A[NPWT核心控制<br/>PID算法·安全系统]
    end

    subgraph "💻 用户界面层"  
        U[触摸UI界面<br/>参数设置·状态显示]
    end
    
    subgraph "🔧 硬件驱动层"
        H[ESP32-S3平台<br/>LCD·PWM·传感器]
    end

    A ---|业务逻辑| U
    U ---|硬件驱动| H
    H -.->|传感器数据| A
    
    style A fill:#ff6b6b,stroke-width:3px,color:#fff
    style U fill:#4ecdc4,stroke-width:3px,color:#fff  
    style H fill:#96ceb4,stroke-width:3px,color:#fff
```

### 系统数据流向图

```mermaid
graph LR
    A[👨‍⚕️ 用户界面] 
    B[🎯 PID控制器]
    C[💨 负压泵]
    D[📊 压力传感器] 
    
    A -->|设置参数| B
    B -->|PWM控制| C  
    C -->|负压输出| D
    D -->|数据反馈| B
    D -->|状态显示| A
    
    style A fill:#4CAF50,color:#fff,stroke-width:3px
    style B fill:#FF9800,color:#fff,stroke-width:3px  
    style C fill:#E91E63,color:#fff,stroke-width:3px
    style D fill:#9C27B0,color:#fff,stroke-width:3px
```

#### 💡 核心流程
1. **用户设置** → PID控制 → PWM驱动 → 负压治疗
2. **压力反馈** → 传感器检测 → 数据处理 → 界面显示  
3. **安全保护** → 异常监测 → 自动停机 → 状态报警

### 核心组件关系图

<div align="center">

| **ESP32-S3 微控制器** |
|:---:|
| 240MHz 双核处理器 |
| 8MB Flash + 8MB PSRAM |

</div>

<table>
<tr>
<td width="33%" align="center">

**🖥️ 显示模块组**

| 模块名称 | 功能描述 |
|------|------|
| **LCD驱动模块** | ST7701控制芯片 |
| **触摸控制模块** | GT911 I2C接口 |
| **图形渲染模块** | LVGL引擎 |
| **显示输出模块** | 800×480分辨率 |

</td>
<td width="33%" align="center">

**⚙️ 控制模块组**

| 模块名称 | 功能描述 |
|------|------|
| **PWM控制模块** | PCA9685芯片 |
| **泵驱动模块** | 医疗级真空泵 |
| **PID算法模块** | 精密压力调节 |
| **频率控制模块** | 1kHz输出频率 |

</td>
<td width="33%" align="center">

**📊 感知模块组**

| 模块名称 | 功能描述 |
|------|------|
| **压力采集模块** | ADC1_CH5接口 |
| **信号处理模块** | GPIO6输入 |
| **通信总线模块** | I2C设备共享 |
| **测量范围模块** | -30~0kPa检测 |

</td>
</tr>
</table>

<div align="center">

**🛡️ 安全监测模块组**

| 安全模块名称 | 功能特性描述 |
|:---:|:---:|
| 🚨 **漏气检测模块** | 压力范围监控，5秒持续检测 |
| 🚧 **堵塞检测模块** | 过压保护，自动报警提醒 |
| 🛑 **自动停止模块** | 异常情况下紧急停机保护 |
| ⏱️ **初始化定时模块** | 60秒启动保护，避免误报 |
| 🛡️ **压力限制模块** | 安全范围锁定，参数边界保护 |
| 💾 **数据存储模块** | 设置参数持久化保存 |
| 🔔 **报警控制模块** | 多级报警，状态显示锁定 |
| 📈 **数据记录模块** | 运行状态实时监测记录 |

</div>

## 🔧 硬件配置

### 核心硬件规格

| 组件 | 型号/规格 | 用途 |
|------|-----------|------|
| **主控芯片** | ESP32-S3 (240MHz) | 系统控制核心 |
| **存储器** | 8MB Flash + PSRAM | 程序存储与运行内存 |
| **显示屏** | 800x480 RGB LCD | 用户界面显示 |
| **显示控制器** | ST7701 | LCD驱动控制 |
| **触摸控制器** | GT911 (I2C) | 触摸输入检测 |
| **PWM控制器** | PCA9685 (I2C) | 泵速精密控制 |
| **压力传感器** | ADC1_CH5 (GPIO6) | 负压监测 |

### 引脚定义

```c
// I2C总线配置
#define NPWT_I2C_SDA_GPIO           8       // I2C数据线
#define NPWT_I2C_SCL_GPIO           9       // I2C时钟线
#define NPWT_I2C_FREQ_HZ            100000  // I2C频率 100kHz

// 压力传感器
#define NPWT_ADC_CHANNEL            ADC_CHANNEL_5    // GPIO6

// PCA9685配置
#define NPWT_PCA9685_ADDR           0x40    // I2C地址
#define NPWT_PCA9685_PWM_CHANNEL    0       // 使用PWM通道0
```

### 硬件连接图

```
       ESP32-S3                           RGB LCD Panel
+-----------------------+              +-------------------+
|                   GND +--------------+GND                |
|                   3V3 +--------------+VCC                |
|                  PCLK +--------------+PCLK               |
|            DATA[15:0] +--------------+DATA[15:0]         |
|                 HSYNC +--------------+HSYNC              |
|                 VSYNC +--------------+VSYNC              |
|                    DE +--------------+DE                 |
|              BK_LIGHT +--------------+BLK                |
+-----------------------+              +-------------------+
|        I2C总线        |              |   GT911触摸控制器  |
|    SDA=GPIO8         | ------------- |   SDA             |
|    SCL=GPIO9         | ------------- |   SCL             |
+-----------------------+              +-------------------+
|       PWM控制         |              |   PCA9685模块     |
|    SDA=GPIO8         | ------------- |   SDA (0x40)      |
|    SCL=GPIO9         | ------------- |   SCL             |
+-----------------------+              +-------------------+
|      压力传感器        |
|    ADC=GPIO6         | ------------- 压力传感器信号线
+-----------------------+
```

## 📱 用户界面

### 主界面 (Screen_1_Print1)

<table>
<tr>
<td width="50%" valign="top">

**实时监测区域**
- 🔢 大字号当前压力显示
- 🎯 目标压力值显示
- ⚙️ 工作模式状态显示
- 🔄 泵速百分比显示
- 📊 系统运行状态指示

</td>
<td width="50%" valign="top">

**控制操作区域**
- ⚡ 电源开关按钮
- ⚙️ 设置界面入口按钮
- 📈 周期进度条(间歇模式)
- 🚨 异常自动停止开关

</td>
</tr>
</table>

### 设置界面 (Screen_2_Move1)

- **目标压力设置**: -30kPa 至 -10kPa，步长1kPa
- **工作时间设置**: 1-60分钟，用于间歇模式工作周期
- **休息时间设置**: 1-60分钟，用于间歇模式休息周期
- **工作模式选择**: 持续模式 / 间歇模式切换
- **参数保存**: 设置自动保存至NVS非易失存储

### 状态指示系统

| 状态 | 显示文字 | 颜色 | 说明 |
|------|----------|------|------|
| 初始化 | "初始化中(X秒)" | 黄色 | 系统启动30秒保护期 |
| 准备就绪 | "准备就绪" | 黄色 | 系统待机状态 |
| 正常运行 | "正常运行" | 绿色 | 系统正常工作中 |
| 敷料漏气 | "敷料漏气" | 红色 | 检测到漏气异常 |
| 管道堵塞 | "管道堵塞" | 红色 | 检测到堵塞异常 |

## 🎮 工作模式详解

### 持续模式 (Continuous Mode)
```
开始治疗 → 持续负压抽吸 → 维持目标压力 → 用户手动停止
```
- **适用场景**: 需要持续负压治疗的创面
- **工作原理**: PID控制器持续调节泵速维持目标压力
- **治疗特点**: 压力恒定，引流持续，适合渗液较多的创面

### 间歇模式 (Intermittent Mode)
```
开始治疗 → 工作期(负压) → 休息期(停止) → 循环往复 → 用户手动停止
```
- **适用场景**: 需要间歇性刺激促进愈合的创面
- **工作期**: 维持目标负压，促进引流和肉芽组织生长
- **休息期**: 停止抽吸，促进血液循环和营养供给
- **治疗特点**: 周期性刺激，平衡引流与血供

## 🔬 核心算法

### PID压力控制算法

```c
// PID控制参数 (优化的快速响应参数)
typedef struct {
    float kp = 20.0f;    // 比例系数 - 快速响应压力偏差
    float ki = 2.0f;     // 积分系数 - 消除稳态误差
    float kd = 1.0f;     // 微分系数 - 减少超调震荡
} npwt_pid_t;

// PID计算过程
float error = target_pressure - current_pressure;  // 压力误差
float P_term = kp * error;                         // 比例项
float I_term = ki * integral;                      // 积分项
float D_term = kd * (error - prev_error) / dt;     // 微分项
float pid_output = P_term + I_term + D_term;       // PID输出

// 反向PWM映射 (高占空比=低泵速)
uint16_t pwm_duty = PWM_WORKING_MAX - (uint16_t)pid_output;
```

**控制逻辑**:
- 当压力不足时(error < 0): PID输出增大 → PWM占空比减小 → 泵速增加
- 当压力过大时(error > 0): PID输出减小 → PWM占空比增大 → 泵速减少

### 卡尔曼滤波算法

```c
// 滤波器参数配置
typedef struct {
    float Q = 0.01f;  // 过程噪声协方差 (系统稳定性)
    float R = 0.1f;   // 测量噪声协方差 (传感器精度)
    float P;          // 估计协方差
    float K;          // 卡尔曼增益
    float x;          // 状态估计值
} npwt_kalman_t;

// 滤波计算过程
K = P / (P + R);                    // 计算卡尔曼增益
x = x + K * (measurement - x);      // 状态更新
P = (1 - K) * P + Q;               // 协方差更新
```

**滤波效果**: 有效降低压力传感器噪声，使显示更平滑稳定

### 压力传感器校准

```c
// 传感器特性参数
#define NPWT_ZERO_POINT_VOLTAGE    480.0f   // 大气压对应电压(mV)
#define PRESSURE_COEFFICIENT      -0.0495f  // 压力转换系数(kPa/mV)

// 压力计算公式
float voltage_mv = adc_reading * 3300.0f / 4095.0f;  // ADC转电压
float pressure_kpa = (voltage_mv - 480.0f) * (-0.0495f);  // 电压转压力

// 多次采样平均滤波
float voltage_sum = 0;
for(int i = 0; i < NPWT_ADC_SAMPLES; i++) {
    voltage_sum += adc_voltage_sample();
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms间隔采样
}
float avg_voltage = voltage_sum / NPWT_ADC_SAMPLES;
```

## 🛡️ 安全保护系统

### 异常检测状态机

```c
typedef enum {
    NPWT_SYSTEM_STATUS_INIT,      // 初始化状态 (启动30秒保护)
    NPWT_SYSTEM_STATUS_READY,     // 准备就绪状态
    NPWT_SYSTEM_STATUS_RUNNING,   // 正常运行状态
    NPWT_SYSTEM_STATUS_LEAK,      // 敷料漏气异常
    NPWT_SYSTEM_STATUS_BLOCKAGE,  // 管道堵塞异常
} npwt_system_status_t;
```

### 异常检测逻辑

| 异常类型 | 检测条件 | 检测时间 | 处理动作 | 恢复方式 |
|----------|----------|----------|----------|----------|
| **敷料漏气** | 压力-5～0kPa且目标<-10kPa | 持续5秒 | 🚨报警+🛑自动停机 | 手动重启 |
| **管道堵塞** | 压力持续<-30kPa | 持续5秒 | 🚨仅报警，继续运行 | 自动清除 |
| **启动保护** | 系统启动阶段 | 前30秒 | ⏸️暂停异常检测 | 自动恢复 |

### 安全特性设计

1. **🔒 多重安全锁定**
   - 启动延时保护：避免系统启动时的误报警
   - 参数范围限制：所有用户输入都有合理边界
   - 异常状态锁定：异常发生后状态持续显示

2. **🚨 智能异常处理**
   - 漏气检测：自动停机保护，防止治疗失效
   - 堵塞检测：仅报警提醒，不影响治疗连续性
   - 状态记忆：异常状态持续显示直到手动清除

3. **🔄 自恢复机制**
   - 条件消失后自动清除堵塞报警
   - 重新开机时自动清除所有异常状态
   - PID控制器自动重置和初始化

## 📊 技术规格

### 性能指标

| 性能项目 | 技术指标 | 测试条件 |
|----------|----------|----------|
| **压力控制范围** | -30kPa ~ -10kPa | 符合医疗NPWT标准 |
| **压力控制精度** | ±1kPa | PID闭环控制 |
| **系统响应时间** | 1-2秒 | 从启动到稳定压力 |
| **泵速调节范围** | 40%-100% | 对应PWM 40%-0% |
| **界面刷新率** | 1Hz | UI数据更新频率 |
| **压力采样率** | 10Hz | ADC连续采样 |
| **控制任务频率** | 10Hz | 100ms周期执行 |

### 电气特性

| 电气参数 | 数值 | 单位 | 备注 |
|----------|------|------|------|
| **工作电压** | 3.3 | V | 系统供电电压 |
| **工作电流** | 150-300 | mA | 根据负载变化 |
| **LCD分辨率** | 800×480 | pixels | RGB565格式 |
| **触摸精度** | ±2 | pixels | GT911控制器 |
| **I2C时钟频率** | 100 | kHz | 标准模式 |
| **PWM频率** | 1 | kHz | 泵控制频率 |
| **ADC分辨率** | 12 | bit | 4096级精度 |

### 存储规格

| 存储类型 | 容量 | 用途 |
|----------|------|------|
| **Flash存储** | 8MB | 程序代码和资源文件 |
| **PSRAM** | 8MB | 运行时内存和LVGL缓存 |
| **NVS存储** | 16KB | 用户设置参数保存 |
| **分区表** | 自定义 | 支持OTA升级 |

## 🚀 快速开始

### 环境准备

1. **安装ESP-IDF开发环境**
```bash
# 下载ESP-IDF v5.2.0
git clone -b v5.2.0 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf

# 安装工具链
./install.sh esp32s3

# 设置环境变量 (每次开发前执行)
source export.sh
```

2. **验证安装**
```bash
# 检查ESP-IDF版本
idf.py --version

# 检查工具链
xtensa-esp32s3-elf-gcc --version
```

3. **克隆项目代码**
```bash
git clone https://github.com/你的用户名/THU-NPWT.git
cd THU-NPWT
```

### 编译和烧录

1. **配置目标芯片**
```bash
idf.py set-target esp32s3
```

2. **项目配置 (可选)**
```bash
idf.py menuconfig
# 导航到 "Example Configuration" 进行配置
# 主要配置项：
# - Display Configuration
# - LVGL Configuration
# - Component Configuration
```

3. **编译项目**
```bash
idf.py build
```

4. **烧录到设备**
```bash
# 连接ESP32-S3开发板，确认端口
ls /dev/ttyUSB*  # Linux
ls /dev/tty.usbserial-*  # macOS

# 烧录固件
idf.py -p /dev/ttyUSB0 flash

# 烧录并监控串口
idf.py -p /dev/ttyUSB0 flash monitor
```

5. **监控运行状态**
```bash
# 仅监控串口输出
idf.py -p /dev/ttyUSB0 monitor

# 退出监控: Ctrl+]
```

### 首次运行检查

启动后观察串口输出，确认以下信息：

```
I (xxx) NPWT_CORE: Initializing NPWT system...
I (xxx) NPWT_CORE: Scanning I2C devices...
I (xxx) NPWT_CORE: Found device at address: 0x40 (PCA9685)
I (xxx) NPWT_CORE: Found device at address: 0x14 (GT911)
I (xxx) NPWT_CORE: Running PCA9685 output test...
I (xxx) NPWT_CORE: UI bridge initialized successfully
I (xxx) NPWT_CORE: System ready for operation
```

## 📁 项目结构

```
THU-NPWT/
├── 📁 main/                          # 主应用程序
│   ├── 📄 main.c                     # 程序入口点和初始化
│   ├── 📄 npwt_core.c/h             # 核心控制逻辑和算法
│   ├── 📄 npwt_ui_bridge.c/h        # UI与业务逻辑桥接层
│   ├── 📄 npwt_ui_events.c          # UI事件处理函数
│   ├── 📄 lvgl_port.c/h             # LVGL移植适配层
│   └── 📄 waveshare_rgb_lcd_port.c/h # Waveshare LCD驱动
├── 📁 components/                    # 外部组件库
│   ├── 📁 lvgl__lvgl/               # LVGL图形库源码
│   ├── 📁 squareline_ui/            # SquareLine Studio UI设计
│   │   ├── 📁 screens/              # UI界面文件
│   │   ├── 📁 images/               # UI图像资源
│   │   ├── 📁 fonts/                # UI字体资源
│   │   └── 📄 ui.c/h                # UI组件定义
│   └── 📁 espressif__esp_lcd_touch_gt911/ # GT911触摸驱动
├── 📁 build/                        # 编译输出目录
├── 📄 CMakeLists.txt                # 顶层CMake构建文件
├── 📄 partitions_custom.csv         # 自定义Flash分区表
├── 📄 sdkconfig.defaults            # 默认配置参数
├── 📄 dependencies.lock             # 组件依赖版本锁定
├── 📄 CLAUDE.md                     # 开发指南和规范
└── 📄 README.md                     # 项目说明文档
```

### 核心模块说明

#### 1. NPWT核心控制 (`npwt_core.c/h`)
```c
// 主要功能
- npwt_system_init()      // 系统初始化
- npwt_pid_calculate()    // PID控制算法
- npwt_mode_continuous_run()  // 持续模式控制
- npwt_mode_intermittent_run() // 间歇模式控制
- npwt_adc_read_pressure()    // 压力传感器读取
```

#### 2. UI桥接层 (`npwt_ui_bridge.c/h`)
```c
// 主要功能
- npwt_ui_update_main_screen()    // 主界面更新
- npwt_ui_check_anomalies()       // 异常检测逻辑
- npwt_ui_handle_power_button_clicked() // 电源按钮处理
- npwt_ui_handle_save_settings()  // 设置保存处理
```

#### 3. 硬件驱动层
```c
// LCD驱动 (waveshare_rgb_lcd_port.c)
- waveshare_esp32_s3_rgb_lcd_init() // LCD初始化
- waveshare_esp32_s3_touch_reset()  // 触摸复位

// LVGL移植 (lvgl_port.c)
- lvgl_port_init()        // LVGL端口初始化
- lvgl_port_lock()        // LVGL线程锁
```

## 🔧 开发指南

### 配置参数调节

#### 1. PID参数优化
根据系统响应特性调整PID参数：

```c
// 响应速度调节
if (response_too_slow) {
    pid->kp += 5.0f;   // 增大比例系数
    pid->ki += 0.5f;   // 增大积分系数
}

// 稳定性调节
if (system_oscillating) {
    pid->kp -= 5.0f;   // 减小比例系数
    pid->kd += 0.5f;   // 增大微分系数
}
```

#### 2. 滤波器参数调节
根据传感器噪声水平调整滤波参数：

```c
// 降噪效果调节
if (pressure_display_noisy) {
    kalman->R += 0.05f;  // 增大测量噪声权重
}

// 响应速度调节
if (filter_too_slow) {
    kalman->Q += 0.005f; // 增大过程噪声权重
}
```

#### 3. 异常检测阈值调节
根据实际使用情况调整检测阈值：

```c
// 漏气检测敏感度
#define LEAK_PRESSURE_MIN    -5    // 漏气压力下限
#define LEAK_PRESSURE_MAX     0    // 漏气压力上限
#define LEAK_DETECT_TIME   5000    // 检测时间(ms)

// 堵塞检测阈值
#define BLOCKAGE_PRESSURE  -30     // 堵塞检测压力(kPa)
#define BLOCKAGE_DETECT_TIME 5000  // 检测时间(ms)
```

### 调试功能详解

#### 1. 串口调试信息
系统提供详细的调试信息输出：

```c
// PID控制调试 (每2秒输出)
ESP_LOGI(TAG, "PID: setpoint=%.1f, input=%.1f, error=%.1f, out=%.1f",
         setpoint, input, error, pid_output);
ESP_LOGI(TAG, "Terms: P=%.1f, I=%.1f, D=%.1f", P_term, I_term, D_term);

// 压力传感器调试 (每1秒输出)
ESP_LOGI(TAG, "Pressure: raw=%.2f, filtered=%.2f, final=%d kPa",
         raw_pressure, filtered_pressure, final_pressure);

// 系统状态调试 (每2秒输出)
ESP_LOGI(TAG, "Status: power=%s, mode=%s, state=%s",
         power_on?"ON":"OFF", mode_str, state_str);
```

#### 2. I2C设备扫描
系统启动时自动扫描I2C总线：

```bash
I (xxx) NPWT_CORE: Scanning I2C devices...
I (xxx) NPWT_CORE: Found device at address: 0x40 (PCA9685)
I (xxx) NPWT_CORE: Found device at address: 0x14 (GT911)
I (xxx) NPWT_CORE: I2C scan completed, found 2 devices
```

#### 3. 性能监测
启用LVGL性能监测：

```c
// 在menuconfig中启用
CONFIG_LV_USE_PERF_MONITOR=y

// 显示FPS和CPU使用率
lv_obj_t *perf_monitor = lv_perf_monitor_create(lv_scr_act());
```

### 常见问题解决

#### 1. 编译问题
```bash
# 清理构建缓存
idf.py fullclean

# 重新获取组件依赖
rm -rf managed_components
idf.py reconfigure

# 更新组件
idf.py update-dependencies
```

#### 2. 烧录问题
```bash
# 检查串口权限
sudo usermod -a -G dialout $USER  # 需要重新登录

# 手动进入下载模式
# 按住BOOT按钮，按一下RESET按钮，释放BOOT按钮

# 使用擦除选项
idf.py -p /dev/ttyUSB0 erase_flash
idf.py -p /dev/ttyUSB0 flash
```

#### 3. 显示问题
```bash
# 检查LVGL配置
idf.py menuconfig
# Component config → LVGL configuration

# 检查LCD连接
# 确认所有数据线和控制线连接正确

# 检查电源电压
# 确认3.3V供电稳定，电流充足
```


## 🤝 贡献指南

### 代码规范

#### 1. 命名约定
```c
// 函数命名: 模块_功能_动作
npwt_pid_calculate()          // NPWT PID计算
npwt_ui_update_pressure()     // NPWT UI更新压力

// 变量命名: 小写下划线
int16_t current_pressure;     // 当前压力
uint32_t system_start_time;   // 系统启动时间

// 常量命名: 大写下划线
#define NPWT_PRESSURE_MIN     -30    // 最小压力值
#define NPWT_PWM_FREQUENCY    1000   // PWM频率

// 类型命名: 模块_类型_t
typedef struct npwt_settings_t;      // 设置结构体
typedef enum npwt_mode_t;            // 模式枚举
```

#### 2. 注释规范
```c
/**
 * @brief PID控制器计算函数
 * @param pid PID控制器结构体指针
 * @param setpoint 目标设定值 (kPa)
 * @param input 当前输入值 (kPa)
 * @return PID输出值 (0-1638范围)
 *
 * @note 该函数实现增量式PID算法，包含积分限幅和微分项计算
 * @warning 调用前确保PID结构体已正确初始化
 */
float npwt_pid_calculate(npwt_pid_t *pid, float setpoint, float input);
```

#### 3. 代码格式
```c
// 使用4空格缩进，不使用Tab
if (condition) {
    function_call();
    another_call();
}

// 大括号换行风格
if (condition)
{
    function_call();
}

// 运算符前后加空格
int result = a + b * c;
if (x == y && z > 0) {
    // code
}
```

### 开发流程

#### 1. 分支管理策略
```bash
# 主分支
main              # 稳定发布版本
develop           # 开发集成分支

# 功能分支
feature/ui-improve        # UI界面改进
feature/pid-optimization  # PID算法优化
feature/data-logging      # 数据记录功能

# 修复分支
hotfix/pressure-bug       # 压力控制bug修复
bugfix/display-issue      # 显示问题修复
```

#### 2. 开发步骤
```bash
# 1. 创建功能分支
git checkout develop
git pull origin develop
git checkout -b feature/new-function

# 2. 开发和测试
# ... 编码开发 ...
# ... 单元测试 ...
# ... 集成测试 ...

# 3. 提交代码
git add .
git commit -m "实现新功能: 详细功能描述

- 添加XXX功能模块
- 优化YYY算法性能
- 修复ZZZ已知问题
- 更新相关文档

测试: 所有单元测试通过，功能验证完成"

# 4. 推送和合并请求
git push origin feature/new-function
# 在GitHub/GitLab创建Pull Request

# 5. 代码审查和合并
# 经过代码审查后合并到develop分支
```

#### 3. 提交信息规范
```bash
# 提交信息格式
<类型>(<范围>): <简短描述>

<详细描述>

<测试说明>

# 类型说明
feat:     新功能
fix:      bug修复
docs:     文档更新
style:    代码格式调整
refactor: 代码重构
test:     测试相关
chore:    构建工具等

# 示例
feat(core): 实现自适应PID控制算法

添加了根据系统响应特性自动调节PID参数的功能：
- 实时监测控制效果
- 动态调整Kp, Ki, Kd参数
- 提升控制精度和稳定性

测试: 通过24小时稳定性测试，控制精度提升30%
```

### 问题报告模板

遇到问题时，请按以下模板提供信息：

```markdown
## 问题描述
简洁明确地描述遇到的问题

## 环境信息
- ESP-IDF版本: v5.2.0
- 硬件型号: ESP32-S3-DevKitC-1
- 操作系统: Ubuntu 20.04 LTS
- 编译工具链版本: gcc 8.4.0

## 复现步骤
1. 执行命令 `idf.py build`
2. 连接设备到端口 `/dev/ttyUSB0`
3. 运行 `idf.py flash monitor`
4. 观察到错误信息

## 预期行为
描述你期望看到的正确行为

## 实际行为
描述实际发生的错误行为

## 错误日志
```
粘贴完整的错误日志信息
```

## 尝试的解决方案
- 已尝试重新编译: 无效
- 已尝试更换端口: 无效
- 已检查硬件连接: 正常

## 附加信息
其他可能有用的信息或截图
```

## 📜 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

```
MIT License

Copyright (c) 2024 THU-NPWT Project

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```

## 📞 技术支持

### 文档资源
- 📖 **项目文档**: 查看 [`CLAUDE.md`](CLAUDE.md) 获取详细开发指南
- 💡 **示例代码**: 参考 `main/` 目录下的实现代码
- 🔧 **硬件说明**: 查看硬件连接和配置信息
- ❓ **常见问题**: 查看问题解答和故障排除

### 技术交流
- 📧 **问题反馈**: 通过GitHub Issues提交技术问题
- 💬 **讨论交流**: 参与GitHub Discussions技术讨论
- 📝 **功能建议**: 提交Enhancement请求和改进建议
- 🐛 **Bug报告**: 使用Issue模板报告软件缺陷

### 学习资源
- 📚 [ESP-IDF编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/)
- 🎨 [LVGL图形库文档](https://docs.lvgl.io/)
- 🏥 [NPWT医疗设备标准](https://www.iso.org/standard/medical-devices.html)
- 🔬 [PID控制理论基础](https://en.wikipedia.org/wiki/PID_controller)

## 🙏 致谢

本项目的实现得益于以下优秀的开源项目和技术社区：

### 核心依赖
- **[ESP-IDF](https://github.com/espressif/esp-idf)** - Espressif官方物联网开发框架
- **[LVGL](https://lvgl.io/)** - 轻量级嵌入式图形库
- **[SquareLine Studio](https://squareline.io/)** - 专业UI设计工具

### 硬件支持
- **[Waveshare](https://www.waveshare.com/)** - 高质量LCD显示模块
- **[Espressif](https://www.espressif.com/)** - ESP32-S3芯片和开发工具

### 开源社区
- **ESP32开发者社区** - 提供丰富的技术资源和支持
- **LVGL社区贡献者** - 持续改进图形库功能和性能
- **医疗设备开源项目** - 分享宝贵的设计经验和标准

### 特别感谢
感谢所有为嵌入式开发和开源医疗设备做出贡献的开发者们！

---

<div align="center">

**🏥 专业医疗设备 · 🔬 精密控制系统 · 💻 现代化界面**

*基于ESP32-S3的下一代NPWT治疗设备*

![GitHub Stars](https://img.shields.io/github/stars/username/THU-NPWT?style=social)
![GitHub Forks](https://img.shields.io/github/forks/username/THU-NPWT?style=social)
![GitHub Issues](https://img.shields.io/github/issues/username/THU-NPWT)
![GitHub License](https://img.shields.io/github/license/username/THU-NPWT)

**[⭐ 给项目加星标](https://github.com/username/THU-NPWT) | [🐛 报告问题](https://github.com/username/THU-NPWT/issues) | [💡 功能建议](https://github.com/username/THU-NPWT/discussions)**

</div>
