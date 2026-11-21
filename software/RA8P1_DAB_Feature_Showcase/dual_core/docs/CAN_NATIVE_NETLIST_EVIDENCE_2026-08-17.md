# CAN 原生网表证据（2026-08-17）

## 证据源与范围

本记录来自 EasyEDA Pro 原生工程 `..\..\..\hardware\eda\DAB.epro2` 内的 `瑞萨杯.epru`，源文件 SHA-256 为 `3CCFE86BFF829EE4CE75A6BE83E0EE56066F4AC9F99CDBB7495A23E06B2CCB69`。连接关系由原生符号引脚、器件变换、WIRE/LINE 几何和网络标号解析得到，不以截图推断。

可复现产物：

- `tools/easyeda_can_netlist_extract.py`：原生工程提取器。
- `tools/easyeda_can_netlist_evidence.json`：完整机器可读结果。

## MCU 到外部接口的连接链

| 方向/网络 | 原生网表连接 |
| --- | --- |
| MCU TX | RA8P1 `P704` → CN2.109 `CAN_TX_MCU` → R26 33 Ω → U4 TCAN3413DR pin 1 TXD |
| MCU RX | U4 pin 4 RXD → R17 33 Ω → `CAN_RX_MCU` → CN2.107 → RA8P1 `P705` |
| 总线高 | U4 pin 7 CANH → `CANH_IC` → U6 ACT1210D-101-2P-TL00 共模扼流圈 → `CANH_EXT` → J1 pin 7 |
| 总线低 | U4 pin 6 CANL → `CANL_IC` → U6 → `CANL_EXT` → J1 pin 2 |
| 公共参考 | J1 pin 3 → GND |
| 浪涌/ESD | D2 PESD2CANFD24L 跨接 CANH_EXT/CANL_EXT，并接地 |
| 终端 | CANH_EXT → R105 60.4 Ω → CAN_TERM_MID → R106 60.4 Ω → CANL_EXT；C67 4.7 nF 从中点接地 |

## 收发器模式与电源

- U4 VIO（pin 5）接 `3V3_RA`，C65 100 nF 去耦。
- U4 VCC（pin 3）接 `3V3_CAN`；`3V3_RA` 经 L2 MPZ1608S601ATA00 磁珠形成 `3V3_CAN`，C64 100 nF 和 C66 1 µF 去耦。
- U4 STB（pin 8）经 R12 `FRC0603F1002TS`（10 kΩ）下拉到 GND，因此硬件默认正常模式，固件没有额外 STB GPIO。
- TXD 侧另有 R104 `FRC0603F1002TS`（10 kΩ）上拉到 `3V3_RA`。

## 固件映射结论

CPU0 应使用 RA8P1 `CANFD0`：`CTX0=P704`、`CRX0=P705`。当前 FSP 配置将 CANFDCLK 设为 PLL2R/6 = 80 MHz，并用 5 × (1 + 23 + 8) 配出 classic CAN 500 kbit/s、75% 采样点。数据相位 2 Mbit/s 只作未来 CAN FD 预留；本次协议固定使用 11 位 classic CAN、8 字节数据帧。

## 证据边界

以上只证明 EDA 设计连接、FSP 静态配置和软件构建关系，不证明损坏板卡上的器件已正确贴装、焊接、电源正常，也不证明 CAN 波形、终端阻值、收发器方向或总线通信已在实物上通过。板卡修复后仍需单独验证静态电阻、供电、示波器波形、ACK、错误计数和双向报文。

在双核工程目录运行：

```powershell
python .\tools\easyeda_can_netlist_extract.py `
  "..\..\..\hardware\eda\DAB.epro2" `
  --output .\tools\easyeda_can_netlist_evidence.json
```
