# 构建与台架准备

项目使用 ESP-IDF 与 CMake，显示部分沿用配套 Waveshare RGB LCD 和 GT911 触摸接口。以下步骤对应源码配置，当前文档完成了静态入口核对；完整构建和上板回归待配套环境执行。

## 环境与源码

[`main/idf_component.yml`](../main/idf_component.yml) 声明 ESP-IDF `>=5.1.0`、LVGL `>8.3.9,<9` 和 GT911 组件依赖。记录实际 SDK 版本、芯片目标、组件锁定结果及 LCD 配置，优先复用原板卡工程的 `sdkconfig`。

```sh
git clone --branch npwt-features https://github.com/Owen1B/PressureCare.git
cd PressureCare
```

## 构建前核对

| 项目 | 对照位置与处理 |
| --- | --- |
| 控制源文件 | `main/CMakeLists.txt` 当前列出 UI/核心文件；需纳入 `npwt_logic.c`、`algorithms/` 和 `hal/` 的实际实现及依赖。 |
| UI 与核心接口 | 对照 `npwt_ui_bridge.c`、`npwt_ui_events.c` 和 `npwt_core.h`；核对目标读取、I2C 扫描和 PWM 测试等调用的声明与定义。 |
| 目标与显示 | 核对 ESP32-S3、RGB 引脚、分辨率、PSRAM 和 LVGL 缓冲配置。 |
| 总线与外设 | 核对触摸与 PCA9685 的 I2C 端口、驱动安装和总线生命周期。 |
| 采样与符号 | 核对 ADC 电压校准、压力传感器零点/系数，以及 PWM 与泵输出的极性。 |

文档保留源码中现有的运行接口，构建目标对齐后再执行完整编译。

## 配置与编译

进入已配置的 ESP-IDF 环境，备份本地配置后执行：

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

记录编译和链接日志，逐项解决源文件、组件依赖与接口问题。当前未提供本版本的成功构建日志或可烧录发布物。

## 首次上电

仅在独立实验台调试。**禁止连接人体或临床管路。** 首次上电前将泵的动力供电与逻辑板隔离，使用仪器核对输出极性、停止状态与启动行为。

`app_main` 在初始化后调用 `npwt_system_start()`；UI 初始化中还包含 PCA9685 输出测试。因此应先检查这些路径及外部驱动电平，确认控制方向，再连接受控测试负载。

构建与上述检查完成后，通过实际串口执行：

```sh
idf.py -p <PORT> flash monitor
```

该命令会写入开发板。串口、供电和烧录目标按实际设备确认。

## 配置入口

[`main/app_config.h`](../main/app_config.h) 保存主要参数：

| 接口或参数 | 当前配置 |
| --- | --- |
| PCA9685 I2C | SDA 8、SCL 9，100 kHz，地址 `0x40`。 |
| PWM 通道与计数 | 通道 0，`0..4095`；HAL 初始化请求 1000 Hz。 |
| 压力采样 | ADC1、通道 5。 |
| 标定公式 | `(voltage_mv - 480) × (-0.0495)` kPa。 |
| 滤波初值 | `Q=0.01`、`R=0.1`。 |
| PID 初值 | `Kp=20`、`Ki=2`、`Kd=1`。 |
| 参数存储 | NVS 命名空间 `npwt_settings`。 |

以上是代码配置，参数与装配、采样周期和台架对象共同确定。控制方向、动态响应和故障路径的核对清单见[验证说明](validation.md)。
