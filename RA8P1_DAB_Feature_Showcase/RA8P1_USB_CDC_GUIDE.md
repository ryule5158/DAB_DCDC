# RA8P1 板载 USB 通信说明

## 已核实的硬件链路

本结论来自开发板原生嘉立创 EDA 网表和核心板 PDF 矢量原理图，不是截图判断。

- USB-C D+：P14.6/P14.8 → `USB_P` → 扩展板 P1.25 → 核心板 J1.25 → RA8P1 `P814/USB_DP`
- USB-C D-：P14.5/P14.7 → `USB_N` → 扩展板 P1.27 → 核心板 J1.27 → RA8P1 `P815/USB_DM`
- USBFS VBUS：USB-C `USB_5V` → AP22615(U1) → `VIN_5V` → 核心板 R180(0Ω) → RA8P1 `P407/USB_VBUS`
- 使用外设：RA8P1 USBFS0，Full-Speed，USB Peripheral CDC ACM
- 固件在 `R_USB_Open()` 后显式开启 D+ 上拉，避免启动瞬间 VBUS 状态尚未稳定导致主机看不到设备
- 开发调试描述符使用 Renesas VID `0x045B`、PCDC PID `0x0002`

开发板工程：
`C:\Users\LENOVO\Desktop\瑞萨CPKHMI-RA8P1扩展板 (1).eprj2`

核心板原理图：
`C:\Users\LENOVO\Desktop\cpk_examples-main\cpkhmi_ra8p1\docs\cpkhmi_ra8p1_v1.1_sch_release.pdf`

## 电脑端使用

1. 用支持数据传输的 USB-C 线连接开发板扩展 USB 口和电脑。
2. Windows 设备管理器中找到新出现的 USB CDC 虚拟 COM 口。
3. 串口助手选择该 COM 口。CDC 的波特率设置不控制 USB 传输速度，可继续填 `115200, 8N1`。
4. 行尾选择 CR、LF 或 CRLF 均可。
5. 发送 `help` 查看命令。

正式首板程序常用命令保持不变：

- `status`：状态、故障和实时功率
- `measure`：单次测量
- `stream on` / `stream off`：连续测量开关
- `zero`：电流零点校准
- `test`：进入保留的隔离外设测试
- `stop`：停止功率传输
- `clear`：排除故障后清锁
- `start`：满足全部安全条件后人工启动

测试固件中的 `adc`、`pwm on/off`、`phase 0/1/2`、`phase counts N`、
`gpio ...` 等命令同样通过 USB CDC 输入。

## 安全行为

- 上电仍默认关闭 PWM 引脚、RUN_REQ 和所有功率路径。
- 正式功率状态中 USB 线断开会立即安全关断，并锁存
  `DAB_FAULT_USB_LINK`。
- 隔离测试模式中 USB 线断开会立即停止八路 PWM，并把受控输出拉低。
- UART9 的 P208/P209 配置未删除，便于回退，但默认程序不再打开 UART9。
