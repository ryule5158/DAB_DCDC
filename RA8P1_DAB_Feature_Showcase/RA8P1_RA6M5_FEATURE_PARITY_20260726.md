# RA8P1 / RA6M5 DAB 首测功能对齐说明（2026-07-26）

## 结论

- 两个平台的串口主命令一致：`status`、`measure`、`stream on/off`、`zero`、`temp`、`pins`、`drivers`、`test`、`stop`、`clear`、`start`、`help`。
- 进入 `test` 后的单字符命令一致：`1` 开 PWM，`0` 关 PWM，`p` 切换 0/160/320 ns 相移，`+/-` 微调，`s` 状态，`i` 引脚，`h` 帮助，`x` 退出。
- 两个平台均为约 300 kHz、50% 逻辑桥臂波形、160 ns 逻辑死区；PWM 不上电自启。
- 测试模式始终保持 `RUN_REQ`、预充、主路和输出保护使能为低，ADC 采样继续运行。
- 当前首测构建的 `start` 被编译锁拒绝；控制器、软启动和完整保护仍保留在源码中，后续经过栅极波形和采样标定后再同时解除两平台锁定。

## RA8P1 固定引脚

| 功能 | RA8P1 | CN2 |
|---|---|---:|
| PRI_A_H / PRI_A_L | P415 / P414 | 83 / 81 |
| PRI_B_H / PRI_B_L | P105 / P104 | 87 / 85 |
| SEC_A_H / SEC_A_L | P915 / P914 | 35 / 37 |
| SEC_B_H / SEC_B_L | P605 / P604 | 39 / 41 |
| ADC_IPRI / ADC_VIN / ADC_IIN | P000 / P001 / P002 | 71 / 101 / 65 |
| ADC_VAFE_HALF / ADC_TEMP_SEC | P003 / P004 | 69 / 33 |
| ADC_ISEC / ADC_VOUT / ADC_IOUT / ADC_TEMP_PRI | P006 / P007 / P008 / P009 | 73 / 51 / 67 / 91 |
| RAW_FAULT_N / IPRI_OC_N / ISEC_OC_N | P401 / P400 / P403 | 59+61 / 79 / 55 |
| PRI_HW_OK / SEC_HW_OK / TEMP_AMB_OT_N | PB06 / PB01 / PB05 | 95 / 49 / 115 |
| RUN / PRE / MAIN / OUT / RESET_N / FAN | P707 / PB00 / PB02 / P706 / PB03 / PB04 | 57 / 97 / 99 / 45 / 47 / 113 |
| TMP1075 I2C SDA / SCL | P514 / P515 | 117 / 119 |
| UART9 RX / TX | P208 / P209 | 开发板调试串口 |

## RA8P1 加速与内存放置

- 编译目标为 Cortex-M85，浮点 Helium MVE 已启用。9 路 ADC 中前 8 路用 MVE 并行换算，第 9 路使用等价标量路径，避免越界向量读取。
- ADC 快照、PWM 相移更新、PWM 紧急关断、测量换算和故障锁存热路径从内部 Flash 启动复制到 ITCM。
- CPU 独占的测量滤波状态放入 DTCM。
- DMAC 环形缓冲明确放在非缓存系统 SRAM；绝不放入 CPU 本地 TCM。

本次链接结果：

- ITCM 热代码：`0x00000000` 至 `0x00000D20`。
- DTCM 初始化数据：`0x20000000` 至 `0x20000010`。
- DTCM 零初始化状态：`0x20000010` 至 `0x20000094`。
- DMAC 环形缓冲：`0x22000200`，长度 `0x0A00`。

## 首次复测顺序

1. 上电后确认串口出现 `start cannot energize hardware`，并发送 `status`、`measure`、`drivers`。
2. 发送 `stream on`，检查 9 路 ADC 和温度换算；发送 `stream off` 停止刷屏。
3. 发送 `test`，再发送 `1`。依次测量上述 8 个 PWM 引脚，确认频率、互补、160 ns 逻辑死区及 160 ns 相移。
4. 发送 `p` 检查 0/160/320 ns 三档相移，发送 `x` 后确认 8 路全部为低。
5. 只有在驱动器输出端实际 `VGS` 死区、保护脚极性和 ADC 工程量都通过后，才讨论解除功率锁；逻辑 PWM 通过不等于 MOSFET 栅极波形通过。
