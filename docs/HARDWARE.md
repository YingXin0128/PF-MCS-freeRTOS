# 硬件与上板指南

## 目标与资源

- 开发板：CT117E-M4；工程目标器件：STM32G431RBTx，对应 STM32G431RBT6。
- CPU：Cortex-M4F；HSI 16 MHz，经 PLL M=2、N=20、R=2，得到 SYSCLK=80 MHz。
- AHB/APB1/APB2 均不分频，TIM2 使用 80 MHz 时钟。
- 启动文件：`MDK-ARM/startup_stm32g431xx.s`。
- 设备宏：`USE_HAL_DRIVER,STM32G431xx`。
- Flash：`0x08000000` 起，128 KiB；工程 RAM：`0x20000000` 起，32 KiB。
- SysTick：1 ms；`SysTick_Handler()` 同时维护 HAL Tick 和已启动调度器的 FreeRTOS Tick。
- SVC / PendSV 由 RVDS Cortex-M4F 端口接管；未使用外设中断来统计流量。

## 接线

| 功能 | 资源 | 说明 |
| --- | --- | --- |
| 压力 | PB15 / ADC2_IN15 | 12 位 ADC，软件触发，247.5 周期采样，8 次均值 |
| 流量 | PB4 / TIM3_CH1 | 上升沿外部计数，16 位计数器，100 ms 读取 |
| 控制输出 | PA1 / TIM2_CH2 | PSC=0、ARR=7999，CCR2=占空比×80 |
| 按键 | PB0、PB1、PB2、PA0 | 对应 B1～B4，低电平按下 |
| LCD 数据 | PC0～PC15 | 并行总线 |
| LCD 控制 | PB5、PB8、PB9、PA8 | 原板卡 LCD 接口 |
| LED | PC8～PC15、PD2 | 74LS573 锁存、低电平点亮 |
| 调试接口 | PA13 / SWDIO、PA14 / SWCLK | 保留给 SWD |

V1.2 按源工程约定：J11 使用 1–2，选择 A02–PB15；J9 短接流量脉冲输入。不同板卡修订应对照实际丝印和原理图确认。

使用外部信号源时连接公共 GND。ADC 输入为 0～3.3 V，频率输入需使用适当的逻辑电平。PA1 输出仅为 MCU 逻辑信号，不直接驱动泵、电机或继电器。若使用独立供电板卡，不要未经确认将调试器供电输出与板卡电源并接。

## 编译与下载

1. 安装 Keil MDK、ARM Compiler 5 和工程要求的 STM32G4 DFP；打开唯一的 `.uvprojx`。
2. 检查目标名称 `PF-MCS-freeRTOS`、器件、Flash 和 RAM 配置。
3. Rebuild，检查 0 Error、0 Warning，选择新生成的 `build/PF-MCS-freeRTOS.hex`。
4. 用板载 CMSIS-DAP 配置 SWD，执行 Download 和 Verify，随后复位运行。
5. 如另用 ST-Link，可在 STM32CubeProgrammer 中选 SWD，连接并核对器件和 Flash 容量，再打开 HEX 执行下载校验。HEX 含地址；BIN 基址为 `0x08000000`。

本次整理只执行编译与主机软件验证，没有执行下载、擦除、选项字节修改或硬件测试。

## 上电检查

复位后进入 MAIN 页、MAN 模式，PWM 初始为 5%。默认 TarP=5.0 BAR、FH=20.0 L/min、FL=10.0 L/min、PL=1.0 BAR、DH=65%。压力和流量首次有效采集在传感器任务开始后的周期点更新。

建议依次验证 LCD 与按键、ADC 电压、频率计数、PWM 输出，再接真实执行器验证反馈方向。示波器检查 PA1 的频率与占空比，信号源检查 PB4 的 790/800/4000/8000/8010 Hz 边界。验证条件和记录表见 `VALIDATION.md`。

## 常见问题

| 现象 | 检查顺序 |
| --- | --- |
| 无法连接器件 | 供电、公共地、SWD 线序、调试时钟；必要时尝试低速或复位下连接 |
| 无压力变化 | PB15 跳线选择、输入电压、是否已在当前电压执行零点校准 |
| 流量始终为 0 | J9、PB4 输入波形、是否低于 800 Hz |
| 参数修改未生效 | 设置页修改为暂存值，按 B1 离开该页才提交 |
| 编译器切换后端口报错 | 本工程使用 ARMCC 5 的 RVDS 端口，ARMCLANG 需要先适配 |
| 复位后累计归零 | 当前无 Flash 参数或累计值持久化 |

连接故障应先检查布线和供电；常规排查不需要修改选项字节或执行整片擦除。
