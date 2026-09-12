# PressureCare · Embedded negative-pressure control prototype

[中文](README.md) · [Control and system design](docs/architecture.md) · [Build notes](docs/build.md)

A personal ESP32-S3 project combining pressure acquisition, scalar Kalman filtering, discrete PID, operating-mode control, and a touchscreen interface. The C firmware uses ESP-IDF / FreeRTOS for control tasks and LVGL for pressure, status, and cycle displays.

Author: Haowen Zheng (Owen). The project focuses on bench-scale feedback control and embedded interface integration.

## Demonstration

<p align="center">
  <a href="https://github.com/user-attachments/assets/2721eb9b-3225-47eb-ae20-bf3e925fd7e3">
    <img src="docs/images/demo.jpg" alt="PressureCare prototype; open the demonstration video" width="780">
  </a>
</p>

https://github.com/user-attachments/assets/2721eb9b-3225-47eb-ae20-bf3e925fd7e3

### Touchscreen interface

<p align="center">
  <img src="docs/images/ui1.png" alt="Main interface" width="48%">
  <img src="docs/images/ui2.png" alt="Settings interface" width="48%">
</p>

The interface provides operating information and settings for target pressure, operating mode, and work/rest durations.

## Technical overview

| Area | Implementation |
| --- | --- |
| Embedded software | C, ESP-IDF, CMake, application/control/HAL separation. |
| Measurement | ADC oneshot, voltage calibration, linear pressure mapping, scalar Kalman filter. |
| Control | Discrete PID with measured time interval, integral clamping, output saturation, actuator polarity mapping. |
| Scheduling | FreeRTOS task, software timer, mutex-protected state and UI callbacks. |
| Interface | LVGL 8, SquareLine UI, 800×480 RGB LCD, GT911 touch. |
| Hardware and storage | ESP32-S3, I2C / PCA9685 PWM, analog pressure sensor, NVS settings. |

## Implementation

The pressure HAL converts ADC readings to voltage and pressure. A scalar filter updates the pressure estimate; the PID uses measured elapsed time for its integral and derivative terms. The actuator layer maps the bounded controller output into PCA9685 counts.

Continuous mode runs the feedback controller. Intermittent mode alternates working and resting states using a one-second timer and resets PID state when a new working cycle starts. The UI layer implements threshold-and-duration checks and displays the associated status.

The [technical notes](docs/architecture.md) explain the filter equations, pressure quantization, PID clamping, signed feedback and PWM polarity, mode transitions, and task timing. These details refer to the source implementation; board-level behavior and performance require bench verification.

## Build entry point

```sh
git clone --branch npwt-features https://github.com/Owen1B/PressureCare.git
cd PressureCare
```

The component manifest requires ESP-IDF `>=5.1.0`, LVGL `>8.3.9,<9`, and the matching display/touch configuration. The current CMake source list and several UI-to-core interfaces need alignment with the control modules before a complete build. See [build notes](docs/build.md).

The startup path calls `npwt_system_start`, and UI initialization includes a PWM output test. Isolate actuator power and verify output polarity before attaching a controlled bench load.

## Source map

- [`main/algorithms/`](main/algorithms/): PID and scalar Kalman filter.
- [`main/hal/`](main/hal/): pressure acquisition and PCA9685 output.
- [`main/npwt_logic.c`](main/npwt_logic.c): control task and mode timing.
- [`main/npwt_core.c`](main/npwt_core.c): shared state, settings and NVS.
- [`main/npwt_ui_bridge.c`](main/npwt_ui_bridge.c): display updates and condition handling.
- [`main/app_config.h`](main/app_config.h): pins, calibration and control parameters.

## Scope and attribution

This is a bench research prototype. Do not connect it to a person or use it for clinical treatment. Demonstration media show the prototype; control accuracy, transient response, and fault handling require independent testing. See [validation notes](docs/validation.md).

Project terms are in [LICENSE](LICENSE); dependencies and media are described in [attribution](docs/attribution.md). The linked technical documents are in Chinese.
