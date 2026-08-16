# RA8P1 DAB 正式业务固件指南

## 1. 交付状态

- 工程名：`RA8P1_DAB_Production`
- 固件标识：`2026-07-30-production-ai-v2`
- MCU：Renesas RA8P1
- PWM：`299760 Hz`，8 路 SPS，互补死区 `160 ns`
- ADC：ADC0 五通道与 ADC1 四通道并行，GPT 硬件触发，DMAC 环形采集
- 控制更新：每 3 个 ADC-DMA 帧更新一次，约 `99.92 kHz`
- 控制接口：开发板扩展 USB CDC，`P814/USB_P`、`P815/USB_N`
- 功率范围：常规命令限制 `20..100 W`；`101..200 W` 必须单独解锁

编译通过只证明源代码、FSP 配置和链接关系一致。20–200 W 功率级、环路参数、传感器比例和保护阈值仍需在实物上分级验证，未经实物记录不能宣称为最终量产认证版。

## 2. 固定引脚合同

以下映射继承已测试工程，新工程未重新分配引脚。

| 功能 | RA8P1 | CN2 |
|---|---:|---:|
| PRI_A_H | P415 | 83 |
| PRI_A_L | P414 | 81 |
| PRI_B_H | P105 | 87 |
| PRI_B_L | P104 | 85 |
| SEC_A_H | P915 | 35 |
| SEC_A_L | P914 | 37 |
| SEC_B_H | P605 | 39 |
| SEC_B_L | P604 | 41 |
| IPRI ADC | P000/AN000 | 71 |
| VIN ADC | P001/AN001 | 101 |
| IIN ADC | P002/AN002 | 65 |
| VAFE_HALF ADC | P003/AN003 | 69 |
| TEMP_SEC ADC | P004/AN004 | 33 |
| ISEC ADC | P006/AN006 | 73 |
| VOUT ADC | P007/AN007 | 51 |
| IOUT ADC | P008/AN008 | 67 |
| TEMP_PRI ADC | P009/AN009 | 91 |
| RAW_FAULT_N/POEG | P401 | 59、61 |
| IPRI_OC_N | P400 | 79 |
| ISEC_OC_N | P403 | 55 |
| RUN_REQ | P707 | 57 |
| IN_PRECHARGE_EN | PB00 | 97 |
| IN_MAIN_EN | PB02 | 99 |
| OUT_PROT_EN | P706 | 45 |
| PROT_RESET_N | PB03 | 47 |
| PRI_HW_OK | PB06 | 95 |
| SEC_HW_OK | PB01 | 49 |
| FAN_EN | PB04 | 113 |
| TEMP_AMB_OT_N | PB05 | 115 |
| TMP1075 SDA/SCL | P514/P515 | 117/119 |

USB CDC：VBUS=`P407`，D+=`P814/USB_P`，D-=`P815/USB_N`。UART9 `P208/P209` 在本固件中不作为控制接口。

## 3. 软件分层

- `dab_board.c`：固定 GPIO 与失效安全顺序；首先拉低 `RUN_REQ`。
- `dab_pwm_hw.c`：GPT0/1/5/8、POEG、8 路固定边沿和相移更新。
- `dab_adc_dma.c`：双 ADC、GPT 触发、DMAC 一致帧。
- `dab_measurements.c`：原理图传感器比例、零点、工程量和滤波。
- `dab_production.c`：统一的软件保护、启动检查、功率/电流限幅和 PI/SPS 控制器。
- `User_Init_Task_entry.c`：业务状态机、USB 命令、故障锁存、遥测和 ROM USB Boot 切换。
- `dab_ra8p1_scope_test.c`：隔离测试模式；不会使能 `RUN_REQ` 或功率路径。
- `dab_usb_console.c`：USB CDC 收发和断线检测。
- `dab_ai_supervisor.c` 与 `src/ai/`：Ethos-U55 异步推理、三模型封装与只降不升的功率上限监督；默认关闭。

测试代码与业务状态机仍是编译隔离的。`RA8P1_PERIPHERAL_TEST_ENABLE=0` 编译正式业务；改为 `1` 才恢复整机隔离外设测试固件。

## 4. 业务状态机

```text
BOOT -> READY -> RESET -> RELEASE -> PRECHARGE -> MAIN
     -> OUTPUT -> SOFTSTART -> RUN
                         RUN <-> PAUSED
任意受控上电状态 --故障/USB断线--> FAULT
任意状态 --control off/stop--> 安全关断
```

关键原则：

1. 上电只到 `READY`，不自动启动。
2. `power N` 是 60 秒有效的单次启动授权。
3. 电流零点有效期为 5 分钟；故障后零点立即失效。
4. 故障后功率命令和 200 W 解锁立即撤销。
5. 运行中只能降低功率上限；提高功率必须先 `pause`。
6. `kp`、`ki`、`vref` 只能在 `READY` 或 `PAUSED` 修改。
7. 不接受 NaN、Inf、超长或越界命令。

## 5. 首次启动步骤

### 5.1 无主功率

1. 使用 Debug/J-Link 或已安装的 USB Boot 烧录 SREC。
2. 连接开发板扩展 USB，打开新出现的 CDC COM。
3. 主功率断开且四路电流真实为零。
4. 发送：

```text
status
zero
status
```

确认：`state=READY`、`fault=0`、`zero=1`、PWM=`0`、硬件保护输入均健康。

### 5.2 20 W 限流首测

1. 输入电源设为 24 V 并使用合理限流；输出接受控负载。
2. 确认硬件急停与示波器可用。
3. 发送：

```text
power 20
control on
```

4. 固件自动预充并从相移 0 缓慢上升，运行时每 200 ms 输出一次状态。
5. 任何异常立即发送：

```text
control off
```

### 5.3 分级提高功率

20 W 完整验证后，先停机或暂停，再依次测试 50 W、100 W。超过 100 W：

```text
control off
zero
unlock 200
power 150
control on
```

200 W 解锁只放宽软件上限，不代表硬件已经通过 200 W 验证。

## 6. USB 命令

| 命令 | 作用 |
|---|---|
| `status` | 状态、电压电流、功率、故障、保护输入和授权时效 |
| `measure` | 单次测量输出 |
| `stream on/off` | 空闲测量流开关 |
| `zero` | READY 状态下校准四路电流零点，同时撤销旧功率授权 |
| `power 20..100` | 设置功率安全上限并建立 60 秒启动授权 |
| `unlock 200` | READY 下解锁 101..200 W 上限 |
| `lock` | 恢复 100 W 上限 |
| `control on` | 通过全部启动检查后执行预充和软启动 |
| `control off` / `stop` | 立即关断并撤销授权 |
| `pause` / `resume` | 保持功率路径时停止/恢复 PWM；恢复仍检查硬件故障 |
| `clear` | 硬件故障消失后清故障；不会自动重启，且要求重新 `zero` |
| `vref 36..48` | READY/PAUSED 下设置输出参考 |
| `kp 0..40` | READY/PAUSED 下设置比例参数，单位 ns/V |
| `ki 0..10` | READY/PAUSED 下设置积分参数，单位 us/(V*s) |
| `test` | READY、fault=0 时进入隔离测试；`h` 查看测试指令，`x` 退出 |
| `boot usb` | 安全关断，5 秒后复位；配合按住 SW202 进入官方 USB Boot |
| `ai on/off/status` | 开关或查看 NPU 辅助监督；默认 OFF，不替代确定性控制与硬件保护 |
| `ai mppt P19,...,P31` | 提交 19–31 V 九点功率扫描，每项 0–555.6 W |

## 7. 软件保护

- Vin 启动范围：`18..33 V`；运行欠压：`17 V`。
- Vout：`min(55 V, Vref+4 V)`。
- 温度：主/副边任一路 `>70 °C` 跳闸。
- ADC-DMA 数据超过 `3 ms` 未更新跳闸。
- `RAW_FAULT_N`、`IPRI_OC_N`、`ISEC_OC_N`、PRI/SEC OK、环境过温和 POEG 均为强制条件。
- 原副边电流、输入输出电流和功率具有随功率命令变化的软件软限幅与硬跳闸值。
- PWM 相移从 0 上升，正常最大 `320 ns`；200 W 解锁后绝对最大 `480 ns`。
- 冷输出时相移按 Vout 分段限制，5 秒内完全无升压会锁存 `NO_RISE`。
- PWM 更新连续错过超过 100 次会锁存 `PWM_UPDATE`。
- FreeRTOS RA 硬件栈监控已在 FSP 配置中启用。

硬件比较器与 POEG 的响应优先于软件。软件保护不能代替熔断器、限流电源、栅极硬件互锁和示波器验证。

## 8. 故障处理

`status` 显示十六进制故障位和首要故障名，例如 `fault=00000080/VIN_UV`。故障处理固定为：

```text
control off
status
```

排除硬件原因后：

```text
clear
zero
power 20
control on
```

禁止在未找出 `RAW_FAULT_N`、POEG、过流或过压原因时反复 `clear`。

## 9. 当前未启用的业务能力

- CAN FD 与 RMII/Ethernet 引脚和底层占位驱动保留，但不参与首版功率控制。
- TMP1075 缺失不会阻止启动；板上独立 `TEMP_AMB_OT_N` 和两路 ISOTMP35 仍参与保护。
- 独立硬件 IWDT 尚未启用；当前启用的是 FreeRTOS RA 硬件栈监控。
- 参数不写入 MRAM，复位后恢复保守默认值。
- 尚未完成硬件在环、200 W 热测试、EMI、异常负载和长时间老化验证。

这些边界是有意保留的安全限制，完成相应实测前不应删除。
