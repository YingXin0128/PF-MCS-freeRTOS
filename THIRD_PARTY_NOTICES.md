# 第三方组件与授权范围

本仓库从已有的 CT117E-M4 上板工程整理，保留源码中的版权及许可证声明。根目录 `LICENSE` 只说明项目自有代码当前未另行授予开源许可证；各第三方组件继续适用其原始条件。

| 组件 | 路径 | 来源依据 | 许可证 / 状态 |
| --- | --- | --- | --- |
| FreeRTOS Kernel V10.3.1 | `Middlewares/Third_Party/FreeRTOS/Source/` | 内核文件头的版本及 Amazon 版权声明 | MIT，见 `LICENSES/FreeRTOS-MIT.txt` |
| Arm CMSIS Core | `Drivers/CMSIS/Include/` | `core_cm4.h` 与编译器适配头的 SPDX 和版本声明 | Apache-2.0，见 `LICENSES/Apache-2.0.txt` |
| STM32G4 设备头文件 | `Drivers/CMSIS/Device/ST/STM32G4xx/Include/` | ST 文件头 | BSD-3-Clause，见 `LICENSES/ST-BSD-3-Clause.txt` |
| STM32G4 HAL / LL 驱动 | `Drivers/STM32G4xx_HAL_Driver/` | ST 文件头 | BSD-3-Clause，见 `LICENSES/ST-BSD-3-Clause.txt` |
| ST 启动、系统及配置文件 | `MDK-ARM/startup_stm32g431xx.s`、`Src/system_stm32g4xx.c`、`Src/stm32g4xx_hal_msp.c`、相关配置头 | 各文件保留的 ST 声明 | 按对应文件头的 BSD-3-Clause 声明执行 |
| CT117E LCD 示例与字体 | `Src/lcd.c`、`Inc/lcd.h`、`Inc/fonts.h` | 原工程与板卡资料附带的 LCD 驱动 | 所提供文件未附可确认的独立再分发许可证，保留原始标注，公开分发前需确认来源授权 |

保留的是实际 ARMCC 5 / Cortex-M4F 目标依赖，不代表随库分发整个 STM32Cube 或 FreeRTOS 软件包。Keil、编译器、器件 Pack 安装包、原理图及赛题 PDF 不在本仓库中。

Apache-2.0 文本来自本机已安装 STM32G4 DFP 携带的 CMSIS 许可证文件；FreeRTOS MIT 文本对应保留的内核源码声明；ST BSD 文本对应 ST 源码中的许可标识。许可证说明不构成对来源不明文件的新授权。
