# RA8P1 DAB ADC+DMA 与 PWM 测试说明

## 当前实现

- DAB PWM：GPT0/1/5/8，250 MHz 计数时钟，周期 834 计数，实际频率约 299.760 kHz。
- 逻辑死区：40 计数，即 160 ns。
- ADC0 顺序转换 AN000～AN004，ADC1 顺序转换 AN006～AN009；两组由 GPT0 同一比较点启动。
- ADC0 扫描结束事件直接触发 DMAC0，每帧搬运 `ADDR[0]..ADDR[9]` 到 64 帧环形缓冲；AN005 只占物理空位，不暴露给上层。
- ADC 和 DMA 不产生 300 kHz CPU 中断。任务通过 `dab_adc_dma_service()` 维护批次并用 `dab_adc_dma_latest()` 取得完整帧。
- 当前临时构建启用了隔离裸板测试镜像；业务状态机、电流零点和 VAFE 合理性判断均不参与测试。
- ADC 获取约 299.760 k 帧/s；串口每 500 ms 自动打印一次九路纯 ADC 电压，打印周期只是显示速度。
- 自动输出格式为 `[TEST][ADC] P000/P5.3/CN2.71=...V ...`；发送 `adc` 可立即再打印一帧。
- GPT0 只在芯片内部运行并触发 ADC；八个 PWM 引脚仍由 GPIO 强制为低。`pwm off` 不会停止 ADC 采样。
- `stream on`/`stream off` 控制自动打印，不改变 ADC+DMA 的后台采样。

## 9 路 ADC 实物测试点

| 信号 | MCU 模拟引脚 | 开发板连接器 | CN2 |
|---|---|---|---:|
| ADC_IPRI | P000 / AN000 | P5.3 | 71 |
| ADC_VIN | P001 / AN001 | P5.5 | 101 |
| ADC_IIN | P002 / AN002 | P5.7 | 65 |
| ADC_VAFE_HALF | P003 / AN003 | P5.9 | 69 |
| ADC_TEMP_SEC | P004 / AN004 | P5.11 | 33 |
| ADC_ISEC | P006 / AN006 | P5.4 | 73 |
| ADC_VOUT | P007 / AN007 | P5.6 | 51 |
| ADC_IOUT | P008 / AN008 | P5.8 | 67 |
| ADC_TEMP_PRI | P009 / AN009 | P5.10 | 91 |

测试源地接控制域 GND，每次只改变一路。建议使用 0.5 V、1.0 V、1.65 V、2.5 V；不要把输入顶在 3.3 V/VREF 边界，更不能超过 3.3 V。

## 8 路 PWM 示波器测试点

上电后 PWM 保持关闭。串口发送 `pwm on` 才开始输出：

| 信号 | MCU / GPT | CN2 |
|---|---|---:|
| PWM_PRI_A_H | P415 / GPT0A | 83 |
| PWM_PRI_A_L | P414 / GPT0B | 81 |
| PWM_PRI_B_H | P105 / GPT1A | 87 |
| PWM_PRI_B_L | P104 / GPT1B | 85 |
| PWM_SEC_A_H | P915 / GPT5A | 35 |
| PWM_SEC_A_L | P914 / GPT5B | 37 |
| PWM_SEC_B_H | P605 / GPT8A | 39 |
| PWM_SEC_B_L | P604 / GPT8B | 41 |

预期约 299.760 kHz、160 ns 逻辑死区，默认次级桥相对一次桥滞后 160 ns。发送 `phase 0`、`phase 1`、`phase 2` 检查 0/160/320 ns 相移；发送 `pwm off` 后 8 路必须全部回到低电平。测试镜像始终保持 `RUN_REQ` 和全部功率路径使能为低。

逻辑 PWM 通过不代表 MOSFET 栅极波形已经通过。接入主功率电源前，必须在驱动器实际输出端复测 `VGS` 死区、幅值、米勒平台和上下管无重叠。
