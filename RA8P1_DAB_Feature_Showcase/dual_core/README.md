# RA8P1 DAB 双核工程

这是从长期测试工程 `C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1` 派生的独立双核版本。测试工程保持只读；本目录使用 FSP 6.4.0 的 Multicore Flat 工程链。

## 应当导入 e² studio 的三个工程

| 工程 | 作用 |
| --- | --- |
| `RA8P1_DAB_DualCore_Solution` | 全局器件、时钟、引脚、内存和安全分区；也是唯一的构建入口 |
| `RA8P1_DAB_DualCore_Solution_CPU0` | Cortex-M85；执行原测试工程的全部 DAB 实时功能 |
| `RA8P1_DAB_DualCore_Solution_CPU1` | Cortex-M33；执行 IPC、硬件 CRC、SDRAM 后台服务和慢速能源策略 |

不要再使用已删除的早期单核副本 `RA8P1_DAB_DualCore_CPU0`。不要把本目录本身当成第四个 e² 工程。

## 当前核间分工

- CPU0：安全输出初始化、GPT 互补 PWM、ADC-B/DMAC、50 kHz 级电压 PI/输出电流 PI、GPIO、UART9 命令与测试模式；安全输出建立后启动 CPU1。空闲优先级 10 ms IPC 任务发送量化测量快照并通过静态 FreeRTOS 队列可靠转发参数，不抢占 DAB 实时控制任务。
- v45c 增加断开主功率后的八路连续 PWM 诊断：`pwmtest on` 固定输出 Q1--Q8、约 300 kHz、400 ns 死区、SPS 外相移 0 ns、inner 0/0 ns；`pwmtest off` 或通用 `off` 停止，`pwmtest show` 查看状态。进入前固件要求板端 `|Vin|<=2 V`、`|Vout|<=2 V`，四路路径 MOS 始终关闭；该命令不进入闭环，不替代带功率验证。
- CPU1：10 ms IPC 服务、100 ms 本地 MPPT/电池充电能源策略、1 s 硬件 CRC 完整性任务、5 s SDRAM 状态任务。CPU1 只产生有界电流参考，不写 PWM、ADC、UART、引脚或最终保护决策；IPC 失联时 CPU0 能把能源参考判为无效。
- 引脚所有权：Solution 只持有共享 SWD 的 P210/P211；CPU0 独占全部板级和 DAB 引脚；CPU1 关闭 Pins 的 `Generate data`、不包含 `r_ioport`，隐藏引脚缓存和活动引脚均为 0，生成的 `pin_data.c` 不含任何 `BSP_IO_PORT_*`，因此不会在启动时重配 UART9 的 P208/P209 或 CPU0 的 IIC/功率引脚。
- Helium：CPU0 的 `dab_measurements.c` 对八路 ADC 原始值和四路标定运算实际生成 MVE/MVE-F 向量指令。
- CMSIS-DSP：当前控制代码没有调用 CMSIS-DSP，CPU0/CPU1 的 FSP stack、库目录和残留 include 路径均已移除；Helium 使用 `arm_mve.h` 和编译器 MVE intrinsic，与 CMSIS-DSP 无关。
- SDRAM：驱动、分配器和快速读写测试已实现，但 `DAB_CPU1_SDRAM_ENABLE=0`。确认实板 SDRAM 型号、容量、引脚和时序前不得打开。
- 能源控制：v44 完整保留 v43/v42/v41/v40 的电压 PI、SPS/EPS/TPS、手动相移、调参、遥测、Ethernet、`arch current|energy`、本地 P&O/增量电导 MPPT、10S 锂离子/12S LFP/36 V 铅酸及自定义充电状态机和 `idir forward|reverse` 输出电流环方向选择。v43 已完成的 PRECHARGE→CC、CC→CV 连续资格和 CV 窗口逻辑保持不变；上电仍为 SPS、单电压 PI、energy/AI/alglog OFF，必须人工启动。TPS 负增益区可显式用 `reverse`，`ALG:` 行末记录 `current_control_direction=1|-1`。AI 未装载模型，电流建议只能降低本地确定性上限，SPS/EPS/TPS 建议仅记录且拥有独立置信度/TTL。
- 2026-08-16 实测合格的 24 V/LFP12/EPS 电流环参数保存在 `src/dab_validated_charger_table.h`。该表与原单电压环 `dab_tuned_parameter_table.h` 分离，默认只读、绝不自动套用；串口 `chgtable show` 可查看 42.0/43.0/43.5/43.7 V CC 和 44.0 V CV 记录。
- v44 新增四个默认关闭、UART 显式调用的确定性控制层：`vinadapt` 对 18--24 V 输入执行方向感知前馈和电流 PI 连续增益调度；`autotune` 用受限小相位扰动识别局部增益方向并给出/加载 Kp、Ki、slew、phase cap；`modswitch` 在保持等效外移相和 PI 积分状态的同时在线渐变 inner shift，并监测偏差/超时回退；`arch cp` + `cpdamp` 提供功率 PI、电流 PI、功率斜率、虚拟电导主动阻尼。四项 OFF 时不改变原目标、调制、基础参数或 DAB/ALG 字段；启用后按 `seq` 追加 `VAD:`、`ATI:`、`MSW:`、`CPD:` 记录。
- v46 离线整合最新已通过记录：生成的电流环/充电表各包含 14 条 SPS/EPS/TPS 种子；未通过的 48 V/1 A SPS 饱和工况没有晋升。CP 外环现在同时受显式 `p` 功率上限、最小有效输出电压和动态 `P/Vout` 电流上限约束，`arch cp` 的调度器复用已验证电流环种子；这仍是软件/构建证据，不是 CP 实物闭环验证。
- CAN v1：CPU0 新增 FSP `r_canfd` + TCAN3413 双向 classic CAN 500 kbit/s，P704/P705 分别为 TX/RX。协议支持 PING、状态/测量读取、立即 STOP，以及仅在 READY/功率关闭时布置 CP 目标；故意不提供 CAN START。详见 [CAN 协议](docs/CAN_PROTOCOL_V1_ZH.md) 和 [原生网表证据](docs/CAN_NATIVE_NETLIST_EVIDENCE_2026-08-17.md)。
- Ethernet 第一阶段：CPU0 使用 FSP `r_rmac/r_rmac_phy`、RMAC1/RMII1、LAN8720A@0 和 FreeRTOS+TCP；静态 IP `192.168.10.100/24`，ICMP Echo 和 TCP JSONL Server `5000`。通信冒烟脚本默认绝不发送 `start`，GUI 等实板 Ping/TCP 通过后再加入。详见 [通信验证说明](docs/ETHERNET_PHASE1_COMMUNICATION_TEST_ZH.md) 和 [原生网表证据](docs/ETHERNET_NATIVE_NETLIST_EVIDENCE_2026-08-15.md)。

## 软件保护选择

v44 保持 v43/v42/v41/v40 已通过带载测试的 `REFERENCE_TEST` 与 `DAB_MANUAL_STOP_TEST_MODE=1U`。RAW/OC/OK、ADC、电流、电压和温度继续记录到串口，但不再由 MCU 因瞬时输入而自动锁停；测试由显式 `off` 停止。该选择不修改板上独立硬件、电源限流或电子负载保护。

编译期配置仍位于 CPU0 的 `src/dab_first_power_config.h`：

```c
#define DAB_SW_PROTECTION_PROFILE DAB_SW_PROTECTION_PROFILE_REFERENCE_TEST
```

当前还同时固定：

```c
#define DAB_MANUAL_STOP_TEST_MODE 1U
#define DAB_POEG_PWM_SHUTDOWN_ENABLE 0U
```

也可以通过编译选项 `-D` 单独覆盖以下任意开关为 0 或 1：

- `DAB_SOFTWARE_AUTO_TRIP_ENABLE`
- `DAB_GPIO_OC_TRIP_ENABLE`
- `DAB_ADC_CURRENT_GUARDS_ENABLE`
- `DAB_VIN_UV_TRIP_ENABLE`
- `DAB_NO_RISE_TRIP_ENABLE`

这些设置只影响 MCU 应用软件，不会自动改写 FSP 外设配置。当前四路 GPT 均为 `GPT_OUTPUT_DISABLE_NONE` 和 `GPT_GTIOC_DISABLE_PROHIBITED`，且 `DAB_POEG_PWM_SHUTDOWN_ENABLE=0` 会把 POEG 驱动调用编译掉。恢复硬件 POEG 钳位必须同时修改 CPU0 FSP 配置和该宏并重新生成/构建。栅极驱动器和板上独立硬件保护不由这些宏控制。

## 实测数据与调参记录

最新版测试工程中的专用目录为 `C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1\DAB_Live_Test_Records`。它只保存按工况合并的 CSV、`INDEX.csv`、工况映射和一份调参记录，不保存零散串口日志。当前已整理 7 个工况、4743 个有效传功样本；后续同工况追加到同名 CSV。转换工具是 `tools/Consolidate-DabValidPowerData.ps1`。

## 构建与核对

优先双击根目录的 `Open-RA8P1-DualCore-E2Studio.cmd` 打开工程。它会显式把本目录作为工作区，避免 e² studio 重启后回到测试工程工作区或桌面上名为 `Electric` 的空工作区。

在 Project Explorer 中右键 `RA8P1_DAB_DualCore_Solution`，选择 `Build Project`（“构建项目”）。Solution 是容器工程，所以顶部锤子按钮变灰是正常现象；不要把 CPU0 或 CPU1 当作整链构建入口。

CPU1 的 Pins/Problems 可能显示 `Pin ... has been configured in preceding project`。这是 CPU1 读取 CPU0 Smart Bundle 后给出的跨核占用提示，不是重复配置错误：双击提示会定位到 CPU1，意思是该引脚已经由 CPU0 持有，CPU1 不应再配置。当前 Ethernet 版本 CPU0 为 `0 errors, 1 warning`，唯一告警来自 FSP 6.4.0 生成的 `r_rmac_phy.c` 未使用形参；三个新增/修改的应用源文件均为 0 告警。CPU1 为 `0 errors, 0 warnings`。跨核引脚黄色提示应保留，不能为了消除它们而让 CPU1 重新生成引脚。

静态等价核对：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Verify-ReferenceParity.ps1
```

默认模式仍执行严格的旧参考等价检查，因此会把有意新增的 Ethernet 模块视为差异。当前版本请使用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Verify-ReferenceParity.ps1 -RequireBuildArtifacts -AllowEthernetPhase1 -AllowEnergyControlV39
```

该模式仍要求全部参考 FSP 属性保持不变，只允许新增 Ethernet 属性和已声明的 v39 控制层；同时检查 CPU1 三个静态任务、能源算法入口、运行时 BENCH 档、CPU0 电流 PI、训练日志、CPU1 引脚隔离、UART9/时钟、POEG 配置、内存分区、真实 Helium 指令和构建产物时间顺序。`Solution/build` 与两个 CPU 工程的 `Debug` 被清理后，必须按 `Solution -> CPU0 -> CPU1` 顺序重建并通过该检查；脚本只读取参考工程，不会修改它。

建议先读 [RA8P1 DAB 双核与特色功能开发手册](docs/RA8P1_DUAL_CORE_AND_FEATURE_DEVELOPMENT_GUIDE_ZH.md)，其中说明了三个 XML、两个 FreeRTOS、IPC/CRC、Helium、SDRAM、构建、下载和排错。简版操作见 [e² studio 双核开发指南](docs/E2STUDIO_DUAL_CORE_GUIDE_ZH.md)，当前证据边界见 [验证记录](docs/VERIFICATION_2026-08-13.md)，最终交付状态见 [最终交付记录](docs/FINAL_HANDOFF_2026-08-15.md)。
