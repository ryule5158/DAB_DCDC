# DAB 主板 CAN 协议 v1

## 物理与链路参数

- classic CAN 2.0，11 位标准帧，500 kbit/s，所有协议帧 DLC=8。
- 主板 DB9：pin 7 CANH、pin 2 CANL、pin 3 GND。
- 主板网表包含 2 × 60.4 Ω 分裂终端。板卡单独断电时 CANH-CANL 设计值约 120.8 Ω；主板和另一端各有约 120 Ω 终端时，总线断电并断开设备电源后的并联测量应约为 60 Ω。
- 原生连接证据见 `CAN_NATIVE_NETLIST_EVIDENCE_2026-08-17.md`。

## 报文 ID

| CAN ID | 方向 | 内容 |
| --- | --- | --- |
| `0x200` | 上位机 → 主板 | 命令 |
| `0x180` | 主板 → 上位机 | 状态 |
| `0x181` | 主板 → 上位机 | 测量值 |
| `0x182` | 主板 → 上位机 | 命令确认 ACK |

固件只在收到命令后应答，不主动发送心跳，避免未连接或无 ACK 的总线上持续重发。

## 命令帧 `0x200`

通用格式：`[版本, 操作码, 序号, payload0, payload1, 0, 0, 0]`。版本固定为 1，序号由上位机产生并原样回显。

| 操作码 | 名称 | payload | 行为 |
| --- | --- | --- | --- |
| `0x01` | PING | 无 | 仅返回 ACK |
| `0x02` | GET_STATUS | 无 | 返回 ACK、`0x180` 和 `0x181` |
| `0x10` | STOP | 无 | 立即关闭 PWM、RUN 和四路路径 MOS，然后返回状态 |
| `0x20` | SET_CP_TARGET | byte3..4 为 0.1 W/LSB 的 little-endian `uint16` | 仅在 READY、PWM/功率关闭、非测试模式时选择 CP 架构并从已验证电流环表装载种子；目标同时成为显式功率上限；只“布置目标”，不会启动功率 |

v1 故意没有 CAN START 命令。CAN 不能把“识别成功/通信成功”升级为功率启动权限；实际启动仍沿用现有显式调试流程。

`SET_CP_TARGET` 的正常范围是 20.0..100.0 W；只有现有高功率解锁已经生效时，固件才允许最高 400.0 W。目标还必须不超过 CP 电流限幅乘以标称输出电压。种子查找或边界校验失败时，固件会返回错误并恢复修改前的配置。

## ACK `0x182`

`[版本, 操作码, 序号, 结果, 状态, 架构, RX丢帧低8位, 错误事件低8位]`

结果码：0 OK，1 版本错误，2 未知操作码，3 DLC 错误，4 状态不允许，5 超范围，6 队列满，7 传输错误，8 没有兼容的已验证种子。

状态码：0 BOOT，1 READY，2 RESET_LOW，3 LATCH_RELEASE，4 MAIN_SETTLE，5 OUTPUT_SETTLE，6 SOFTSTART，7 RUN，8 PAUSED，9 PATH_DIRECT，10 TEST_MODE，11 FAULT。

架构码：0 单电压 PI，1 输出电流 PI，2 CPU1 能源监督 + 电流 PI，3 CP 功率 PI + 电流 PI。调制码：0 SPS，1 EPS，2 TPS，3 AUTO。

## 状态与测量

状态帧 `0x180`：`[版本, 状态, 架构, 有效调制, flags, fault0, fault1, fault2]`。flags 位 0..7 依次为测量有效、控制使能、PWM 使能、测试模式、CAN bus-off、CP 阻尼使能、已装载种子、软件自动跳闸编译使能。

测量帧 `0x181`：

| 字节 | 类型/比例 | 含义 |
| --- | --- | --- |
| 0..1 | little-endian `uint16`, 0.1 V/LSB | Vin |
| 2..3 | little-endian `uint16`, 0.1 V/LSB | Vout |
| 4..5 | little-endian `int16`, 0.01 A/LSB | Iout，支持反向符号 |
| 6..7 | little-endian `int16`, 0.1 W/LSB | Pout，支持反向符号 |

## 上位机最小客户端

`tools/dab_can_host.py` 支持 `ping`、`status`、`stop`、`set-cp W`；只有访问适配器时才需要安装 `python-can`。先做无硬件编解码自测：

```powershell
python .\tools\dab_can_host.py --self-test
```

修复板卡后，以 SLCAN 串口适配器 COM3 为例：

```powershell
python .\tools\dab_can_host.py --interface slcan --channel COM3 status
python .\tools\dab_can_host.py --interface slcan --channel COM3 set-cp 100
python .\tools\dab_can_host.py --interface slcan --channel COM3 stop
```

不同 USB-CAN 设备应按其 `python-can` 后端修改 `--interface/--channel`，不能假定所有设备都使用 SLCAN。
