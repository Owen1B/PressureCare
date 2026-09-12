# PressureCare · 嵌入式负压控制原型

[English](README.en.md) · [控制算法与系统设计](docs/architecture.md) · [构建说明](docs/build.md)

基于 ESP32-S3 的个人嵌入式项目，围绕负压控制台架实现压力采集、Kalman 滤波、离散 PID、连续与间歇运行，以及触摸屏参数设置。软件使用 C 编写，采用 ESP-IDF / FreeRTOS 组织控制任务，通过 LVGL 显示压力、工作状态和周期进度。

项目重点是传感器标定、反馈控制与人机交互的集成。作者：郑皓文（Owen）。

## 项目演示

<p align="center">
  <a href="https://github.com/user-attachments/assets/2721eb9b-3225-47eb-ae20-bf3e925fd7e3">
    <img src="docs/images/demo.jpg" alt="PressureCare 硬件原型，点击查看演示视频" width="780">
  </a>
</p>

<p align="center">硬件原型与触摸屏，点击图片查看演示。</p>

### 实机视频

https://github.com/user-attachments/assets/2721eb9b-3225-47eb-ae20-bf3e925fd7e3

### 操作界面

<p align="center">
  <img src="docs/images/ui1.png" alt="主界面" width="48%">
  <img src="docs/images/ui2.png" alt="设置界面" width="48%">
</p>

<p align="center">主界面显示运行信息，设置界面调整目标压力、运行模式及工作与休息时间。</p>

## 技术栈

| 层级 | 技术与用途 |
| --- | --- |
| 嵌入式软件 | C、ESP-IDF、CMake；应用、控制算法与硬件抽象层分别组织。 |
| 测量与估计 | ADC 单次采样、ADC 电压校准、压力线性标定、标量 Kalman 滤波。 |
| 控制算法 | 带实际采样间隔的离散 PID、积分限幅、输出饱和、执行器极性映射。 |
| 实时系统 | FreeRTOS 控制任务、软件定时器、互斥锁、状态机和 UI 回调。 |
| 显示与交互 | LVGL 8、SquareLine UI、800×480 RGB LCD、GT911 触摸输入。 |
| 硬件与存储 | ESP32-S3、I2C / PCA9685 PWM、模拟压力传感器、NVS 参数保存。 |

## 功能与控制路径

触摸屏设置目标压力和运行时长，控制任务读取压力、更新滤波状态并计算泵控制量。连续模式保持闭环调节；间歇模式通过工作和休息状态切换控制输出。设置参数通过 NVS 保存，界面提供压力、泵输出和周期进度的显示。

```mermaid
flowchart LR
    UI[触摸设置] --> SP[目标与模式]
    ADC[ADC 与电压校准] --> P[压力换算]
    P --> KF[Kalman 滤波]
    KF --> PID[离散 PID]
    SP --> PID
    PID --> PWM[PWM 映射与限幅]
    PWM --> Plant[泵与台架]
    Plant --> ADC
    KF --> Display[LVGL 状态显示]
```

### 压力测量与滤波

压力读数先经过 ADC 电压校准，再按传感器零点与比例系数换算为相对压力。标量 Kalman 滤波使用过程噪声和测量噪声参数控制估计的更新幅度，为控制器提供平滑后的反馈量。

当前测量接口与运行状态均使用整数 kPa，滤波内部采用浮点数。量化位置、噪声参数与动态响应之间的关系见[压力估计](docs/architecture.md#压力估计)。

### 离散 PID 与执行器映射

PID 使用实际调用间隔计算积分和差分项，通过积分限幅约束累积误差，再对输出执行饱和处理。泵控制层将输出映射到 PCA9685 的 12 位计数范围；压力符号、控制误差与驱动极性需要联合核对。

计算公式、输出约束及其参数含义见[反馈控制](docs/architecture.md#反馈控制)。

### 运行模式与异常条件

控制状态包括空闲、工作和休息。间歇模式使用秒级计时器管理周期，在新工作周期重置 PID 的积分和历史误差。异常处理采用条件阈值与持续时间判断，并在界面显示相应状态。

控制节拍、异常检测的调用位置和状态切换细节见[任务与状态](docs/architecture.md#任务与状态)。

## 构建入口

```sh
git clone --branch npwt-features https://github.com/Owen1B/PressureCare.git
cd PressureCare
```

组件清单要求 ESP-IDF 5.1.0 及以上、LVGL `>8.3.9,<9`，并依赖配套的 LCD 与触摸配置。当前 CMake 源文件列表和部分 UI 调用接口需要先与控制模块对齐；配置、构建及台架准备步骤见[构建说明](docs/build.md)。

## 代码导航

| 路径 | 内容 |
| --- | --- |
| [`main/algorithms/`](main/algorithms/) | PID 与标量 Kalman 滤波。 |
| [`main/hal/`](main/hal/) | ADC 压力读取、PCA9685 通信及 PWM 输出。 |
| [`main/npwt_logic.c`](main/npwt_logic.c) | 控制任务、连续/间歇模式与周期计时。 |
| [`main/npwt_core.c`](main/npwt_core.c) | 共享状态、设置接口与 NVS 存储。 |
| [`main/npwt_ui_bridge.c`](main/npwt_ui_bridge.c) | UI 数据桥接、异常条件与交互状态。 |
| [`main/app_config.h`](main/app_config.h) | 引脚、标定系数、滤波与控制参数。 |
| [`docs/images/`](docs/images/) | 原型照片和界面截图。 |

## 使用范围与资料

本项目用于台架实验和嵌入式控制研究，禁止接入人体或用于临床治疗。演示材料记录原型效果；控制精度、超调、异常响应和长期稳定性应通过独立台架测量确认，具体项目见[验证说明](docs/validation.md)。

[算法与系统设计](docs/architecture.md) · [构建说明](docs/build.md) · [验证说明](docs/validation.md) · [作者与依赖](docs/attribution.md) · [LICENSE](LICENSE)
