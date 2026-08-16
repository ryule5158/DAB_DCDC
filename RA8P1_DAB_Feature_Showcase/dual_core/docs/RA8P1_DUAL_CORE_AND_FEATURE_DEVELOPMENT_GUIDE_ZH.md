# RA8P1 DAB 双核与特色功能开发手册

本文针对本目录中的 FSP 6.4.0 Multicore Flat 工程，不是泛泛的双核示例。目标是保持长期测试工程的 DAB 行为不变，同时把适合本项目的 RA8P1 能力放到不会干扰功率实时链的位置。

## 1. 先看结论

- CPU0 是 Cortex-M85，保留原测试工程的 PWM、ADC/DMAC、控制、UART9、命令和保护语义；Helium/MVE-F 用于测量换算。
- CPU1 是 Cortex-M33，运行独立 FreeRTOS，承担 IPC、硬件 CRC 和默认关闭的 SDRAM 服务，不直接控制功率外设。
- 两个 CPU 有各自的向量表、链接地址、启动代码、FreeRTOS 调度器和 ELF，能够在芯片上真正同时运行；这不是在一个核上轮流切换的“双任务”。
- 两个 FreeRTOS 不是一个 SMP 系统。一个核上的队列、信号量和任务句柄不能直接给另一个核使用；跨核通信必须走 IPC 或经过严格设计的共享内存。
- 默认 `REFERENCE_TEST` 档位保持参考工程的 manual-stop-only 应用软件语义。它不关闭 POEG、栅极驱动器或板上硬件保护。
- SDRAM 驱动已经编译，但 FSP SDRAM 控制器和应用访问宏都保持关闭。没有确认实板 SDRAM 物料、容量、走线和时序前，不允许打开。
- 测试固件不包含 AI 模型。Ethos-U55 是芯片优势，但在当前测试功能中没有合理工作负载，因此不为了展示而接入。

## 2. 为什么有三个工程和三个图形化 XML

工程链如下：

```text
RA8P1_DAB_DualCore_Solution/solution.xml
          │  全局器件、时钟、引脚、Flash/RAM/外部存储分区
          ▼
Solution/build/RA8P1_DAB_DualCore_Solution.sbd
          │
          ▼
RA8P1_DAB_DualCore_Solution_CPU0/configuration.xml
          │  M85 外设、FreeRTOS、线程、CPU0 源码和链接
          ▼
CPU0/Debug/RA8P1_DAB_DualCore_Solution_CPU0.sbd + CPU0 ELF
          │
          ▼
RA8P1_DAB_DualCore_Solution_CPU1/configuration.xml
             M33 外设、FreeRTOS、线程、CPU1 源码和链接
          ▼
CPU1 ELF
```

三个 XML 的职责不同：

| 文件 | 应该修改的内容 | 不应该放进去的内容 |
| --- | --- | --- |
| `Solution/solution.xml` | 芯片型号、全局时钟、引脚、双核 Flash/RAM/SDRAM 分区 | C 业务代码、FreeRTOS 任务函数 |
| `CPU0/configuration.xml` | M85 的 GPT、POEG、ADC-B、DMAC、UART9、IIC、CMSIS-DSP、CPU0 FreeRTOS | CPU1 线程和 CPU1 专属服务 |
| `CPU1/configuration.xml` | M33 的 FreeRTOS 和 CPU1 线程 | CPU0 已拥有的 PWM、ADC、UART9 |

当前 CPU1 关闭 Pins 的 `Generate data`，公开配置和隐藏缓存中的有效引脚均为 0，而且没有 `r_ioport`。Solution 只拥有 P210/P211 的共享 SWD，所有 UART9、IIC、PWM、ADC、保护和板级 GPIO 都由 CPU0 独占。CPU1 的 `ra_gen/pin_data.c` 因而不含 `BSP_IO_PORT_*`，M33 启动时不会再次写 PFS 去覆盖 M85 的引脚选择。

Multicore Flat 子工程本来就不重复保存全局时钟。本工程中 CPU0 和 CPU1 的 `configuration.xml` 都有 0 个 `raClockConfiguration` 节点，103 项全局时钟由 Solution 持有并通过 `.sbd` 向下游传播。这不是缺配置。

`Solution` 也不是普通 C/C++ 工程：它没有 `main()`，不会直接链接 ELF，而是一个容器和依赖链构建入口。因此选中 Solution 后，顶部锤子按钮可能是灰色的。正确构建方法是：

1. 在 Project Explorer 中右键 `RA8P1_DAB_DualCore_Solution`。
2. 选择 `Build Project`（中文界面为“构建项目”）。
3. 等待 Solution、CPU0、CPU1 依次完成。

Renesas 官方也明确说明 Solution 必须从右键菜单触发构建；部分 e² studio 版本不支持用锤子按钮构建 Solution。

## 3. 本工程的内存和镜像关系

当前 Solution 的主要分区为：

| 资源 | CPU0 | CPU1 |
| --- | --- | --- |
| Code Flash | `0x00000000`，大小 `0x80000` | `0x00080000`，大小 `0x80000` |
| 片上 RAM | 偏移 `0x000000`，大小 `0xEA000` | 偏移 `0x0EA000`，大小 `0xEA000` |
| TCM | 128 KiB ITCM + 128 KiB DTCM | 64 KiB CTCM + 64 KiB STCM |
| SDRAM 逻辑预留 | 外部区前 64 MiB | 外部区后 64 MiB，应用基址 `0x6C000000` |

SDRAM 的逻辑预留不代表板上真的有 128 MiB SDRAM，也不代表 FSP 时序已经正确。当前 `solution.xml` 的 `config.bsp.fsp.sdram.enabled` 为 disabled，CPU1 的 `DAB_CPU1_SDRAM_ENABLE` 为 0、容量为 0，所以不会访问该地址。

TCM 不只是预留：`dab_accel.h` 把控制、保护、PWM、ADC/DMAC 和测量热点放入 `.itcm_code_from_flash`，把测量状态放入 `.dtcm/.dtcm_from_flash`；最新 map 文件显示 `dab_measurements_update` 位于 ITCM、`s_zero/s_filtered` 位于 DTCM。DMA 环形缓冲区则位于 `0x22000020` 的 `.ram_nocache`，没有错误地放进 CPU 本地 TCM。当前仍缺板上周期测量，所以不能把“正确链接”夸大为已经量化的加速比。

## 4. 两个 CPU 和两个 FreeRTOS 到底怎样运行

RA8P1 复位后先执行 CPU0。CPU1 仍保持复位，直到 CPU0 调用：

```c
R_BSP_SecondaryCoreStart();
```

本工程把这句放在 `dab_dual_core_cpu0_start()` 中，并且只在 CPU0 已执行 `dab_board_initialize_safe()` 或 `test_force_safe_outputs()` 后调用。这保证副核启动动作不会早于功率输出进入安全状态。

随后发生的是：

1. CPU0 从自己的向量表启动自己的 FreeRTOS 调度器。
2. CPU0 释放 CPU1 复位。
3. CPU1 从 `0x00080000` 对应的镜像入口和向量表启动自己的 FreeRTOS 调度器。
4. M85 和 M33 在各自时钟域上同时取指、处理中断和调度任务。
5. 两个核通过 RA8P1 IPC FIFO 交换 32 位消息。

所以“真正并行”的含义是：CPU0 可以在处理 DAB 控制/串口任务时，CPU1 同时做 CRC 或内存服务。它不是一个任务迁移到任意核的 SMP FreeRTOS；CPU0 的任务永远由 M85 执行，CPU1 的任务永远由 M33 执行。

每个核分别拥有：

- 自己的 SysTick、NVIC 和中断上下文；
- 自己的 `FreeRTOSConfig.h`、任务栈和静态任务控制块；
- 自己的堆配置。本工程两核都禁止 FreeRTOS 动态分配，业务任务使用 `xTaskCreateStatic()`；
- 自己的故障现场和调试会话。

## 5. 当前 CPU0、CPU1 和任务分工

| 核/任务 | 周期或触发 | 当前职责 | 明确不做什么 |
| --- | --- | --- | --- |
| CPU0 `User_Init_Task` | 原工程主循环 | 原测试工程全部 DAB 功能、UART9 命令、状态机、PWM、ADC/DMAC、控制与诊断 | 不把实时控制转交 CPU1 |
| CPU0 `CPU0 IPC` | 10 ms、空闲优先级 | 收发 IPC、记录对端状态、1 s 心跳 | 不抢占优先级 1 的 DAB 主任务，不决定启停和保护 |
| CPU1 `new_thread0` | 10 ms | IPC 服务、心跳和状态镜像 | 不访问 GPT/ADC/UART9 |
| CPU1 `CPU1 Integrity` | 1 s | RA8P1 硬件 CRC32、自检、状态上报 | CRC 不是密码学签名 |
| CPU1 `CPU1 Memory` | 5 s | SDRAM 状态；宏打开后才允许快速测试 | 默认不访问外部 SDRAM |

这种拆分保留了测试工程的功率实时链。把 ADC、PWM 更新或保护判断跨核拆开会引入 IPC 延迟、消息丢失和缓存一致性问题，无法再声称测试效果相同，因此当前没有这样做。

## 6. IPC 和 CRC 的具体实现

### 6.1 IPC

IPC 是 Inter-Processor Communication，即核间通信。本工程使用 RA8P1 的硬件消息 FIFO，不用共享裸指针传递当前状态。

当前 32 位消息格式为：

```text
31          28 27          20 19                         0
+--------------+--------------+----------------------------+
| type, 4 bit  | seq, 8 bit   | payload, 20 bit            |
+--------------+--------------+----------------------------+
```

- `type` 区分心跳和状态；
- `seq` 用于观察是否漏包或乱序；
- `payload` 携带时间低 20 位或 CRC 状态；
- `peer_seen`、`peer_stale`、收发计数保存在两个核各自的状态结构中；
- 目前是 10 ms 轮询服务，简单、可控，不改变 CPU0 现有中断优先级。以后若确有低延迟需求，可在 FSP 中配置 `r_ipc` 接收中断，但必须重新做中断优先级和最坏延迟验证。

`__DMB()`/`__DSB()` 用于约束寄存器和内存访问顺序。它们不等于 Cache clean/invalidate；以后若通过可缓存 SRAM/SDRAM 共享大数组，还要指定缓冲区所有者、生命周期、Cache 维护和握手协议。

### 6.2 CRC

CRC 是 Cyclic Redundancy Check，即循环冗余校验。本工程在 CPU1 上启动 RA8P1 的 CRC 外设，用 CRC-32、LSB-first、`0xFFFFFFFF` 种子计算 4 字节对齐的数据，并在初始化时用固定 16 字节已知答案做自检。

它适合：

- 周期检查参数块或配置块是否被意外改写；
- 检查核间大数据块传输是否损坏；
- 把完整性计算从 CPU0 控制链移到 CPU1。

它不适合证明固件来源可信，也不能阻止恶意篡改。真正的安全启动/固件认证应使用 RA8P1 的 RSIP、TrustZone 和安全启动方案，属于以后独立的安全版本，不能混入“与测试工程效果一致”的当前版本。

## 7. Helium/MVE-F 在电源 DSP 中怎样使用

Helium 是 Cortex-M85 的 M-profile Vector Extension（MVE）。MVE-F 可以一次处理多路整数或浮点数据，适合 ADC 多通道标定、数字滤波、均方根、功率计算和频域分析。

当前 `dab_measurements.c` 已在一帧八路 ADC 原始值上使用：

- `vld1q_u16` 一次装载 8 路 16 位采样；
- `vmovlbq_u16`/`vmovltq_u16` 展宽偶数/奇数通道；
- `vcvtq_f32_u32` 转换为浮点；
- `vmulq_n_f32` 完成成组缩放；
- 随后对四路校准量做向量乘加。

最新 `dab_measurements.o` 的反汇编实际包含 `vldrh.u16 q0`、`vmovlb.u16 q1`、`vcvt.f32.u32 q*`、`vmul.f32 q*`、`vfma.f32 q*` 和 `vstrw.32 q*`，所以这里不是只写了 intrinsics 却走标量条件分支。

标量回退路径仍保留，因此算法语义不变。要继续扩展 DSP，推荐顺序是：

1. 先固定输入/输出和误差容限；
2. 用标量版本建立黄金结果；
3. 优先用直接 Helium intrinsic 实现并验证；只有确实需要现成 FIR/IIR/RMS 算法时才重新加入 CMSIS-DSP；
4. 比较数值误差、目标文件反汇编和板上周期；
5. 再决定是否放入 TCM。

不要把单次 PI 标量更新硬改成向量代码；数据并行度不足时不会有实际收益。

当前 CPU0/CPU1 都没有应用代码调用 CMSIS-DSP，因此 FSP stack、`ra/arm/CMSIS-DSP` 和 CPU0 `.cproject` 的残留 include 路径均已移除。Helium 通过 `arm_mve.h` 和 LLVM MVE intrinsic 生成真实向量指令，不依赖 CMSIS-DSP。以后若重新加入库，必须有具体算法调用和目标文件证据，不能只为“体现特色”增加数百个无用源码。

## 8. SDRAM 驱动怎样启用

当前驱动提供状态检查、对齐分配和保存-写入-校验-恢复的快速测试，但双重门控保持关闭。启用前必须完成：

1. 核对原理图和 BOM，确认 SDRAM 型号、数据宽度、行列地址、Bank、容量和电压。
2. 在 Solution 的 BSP/SDRAM 页面配置真实 `TRAS/TRCD/TRP/TWR/TCL/TRFC`、刷新周期、地址移位和总线宽度。
3. 配置并核对 SDRAM 全部引脚，处理 e² studio 2025-12 的 project-chain 引脚传播已知问题。
4. 从 Solution 右键 `Build Project`，确认 `BSP_CFG_SDRAM_ENABLED=1`。
5. 在 CPU1 `dab_dual_core_config.h` 中填写真实容量，再把 `DAB_CPU1_SDRAM_ENABLE` 改为 1。
6. 第一次只在无功率状态运行小范围 destructive quick test，并用调试器观察 `g_cpu1_service_status.sdram`。
7. 通过数据总线 walking-1/0、地址线、全容量和长时间刷新测试后，才允许给 DSP 环形缓冲区使用。

若缓冲区被两个核共享，还必须处理 M85/M33 Cache 一致性。当前驱动故意没有伪装成“已验证 SDRAM”。

## 9. RA8P1 特色能力的取舍表

RA8P1 系列的官方能力包括最高 1 GHz Cortex-M85、最高 250 MHz Cortex-M33、Helium/MVE-F、Ethos-U55 NPU、2 MB SRAM（含 TCM 和 Cache）、16 位 ADC、GPT/POEG、外部 SDRAM/OSPI、Ethernet/TSN、USB、CAN FD、TrustZone 和 RSIP。具体封装与板级连接仍以本机型 `R7KA8P1KFLCAC`、原理图和 BOM 为准。

| 特色 | 本工程状态 | 对 DAB 的价值 | 当前决定 |
| --- | --- | --- | --- |
| M85 + M33 双核 | 已用 | 实时控制与慢速完整性/存储服务物理隔离并行 | 保留 |
| Helium/MVE-F | 已用 | 多通道 ADC、滤波和功率 DSP 加速 | 已接入测量路径 |
| 两套独立 FreeRTOS | 已用 | 周期任务、静态栈、职责隔离 | 保留，不做 SMP 幻想 |
| IPC FIFO | 已用 | 心跳、状态和以后大数据握手 | 当前轮询驱动足够 |
| 硬件 CRC | 已用 | 参数/状态完整性，卸载 CPU0 | CPU1 周期任务 |
| ADC-B + DMAC | 沿用参考工程 | 确定性多通道采样 | CPU0 独占 |
| GPT + POEG | GPT 已用；POEG 模块保留但测试镜像旁路 | GPT 负责互补 PWM/死区；POEG 可用于未来保护版本 | 当前四路 GPT 禁止输出钳位，POEG 驱动不打开 |
| TCM/Cache | 热点代码/状态已放入 ITCM/DTCM，DMA 在 non-cache SRAM | 降低热点访问抖动并避免 DMA Cache 错误 | 保留；加速比等板上周期证据 |
| 外部 SDRAM | 驱动已写，默认关闭 | 长窗口 DSP、记录、模型张量 | 等硬件确认 |
| Ethos-U55 NPU | 未接入测试固件 | 将来可做波形分类/异常识别 | 当前无模型，不改变测试模式 |
| RSIP/TrustZone/安全启动 | 未接入 | 量产固件认证、密钥保护 | 单独安全版本实施 |
| OSPI XIP/DOTF | 未接入 | 大模型/资源外置和实时解密 | 无板级需求，不添加空驱动 |
| Ethernet TSN/USB/CAN FD | 未新增 | 高速遥测、同步、现场总线 | 只有接口需求和硬件连接确认后再做 |
| 图形/摄像头/音频接口 | 未用 | 与当前 DAB 测试无直接价值 | 不加入 |

“用上芯片优势”不等于把所有外设都初始化。未连接的总线、没有模型的 NPU、未确认时序的 SDRAM如果强行启用，只会增加引脚冲突、时钟风险和调试面，不会提高电源控制质量。

## 10. 配置、生成和完整构建

### 10.1 修改全局时钟或引脚

1. 打开 Solution 的 `solution.xml`。
2. 修改并保存。
3. 右键 Solution，选择 `Build Project`。
4. 检查 Console 中 CPU0、CPU1 各自结束为 0 error。
5. 运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Verify-ReferenceParity.ps1 -RequireBuildArtifacts
```

脚本会检查：

- 参考工程源码和逐项白名单化的双核/测试适配点；
- 1051 项 FSP 模块属性、CPU0 的 162 项板级引脚以及移到 Solution 的 6 项 SWD 物理分配；
- Solution 正式 XML 与隐藏 `.secure_xml` 的 103 项时钟；
- Solution `.sbd` 内实际传播的 103 项时钟；
- CPU1 关闭引脚生成、没有 `r_ioport`、活动及隐藏引脚缓存均为 0、CPU1 生成引脚表为空；
- CPU0、CPU1 的 Smart Bundle 路径和构建时间顺序；
- `PLL2=XTAL`、`SCICLK=PLL2R`、UART9 `BRR=64`/0.160%；
- 两个 ELF、三个 `.sbd`、两个 `bsp_linker_info.h`；
- Option Function 设置与参考工程一致、Flash/RAM/SDRAM 分区不重叠；
- CMSIS-DSP 已移除、POEG 不钳位、手动停机编译锁、IPC 低优先级；
- IPC、CRC、SDRAM、Helium、TCM 和 RTOS 的源码/二进制证据。

### 10.2 修改 CPU0 外设或线程

1. 打开 CPU0 `configuration.xml`。
2. 只改 CPU0 所有的外设/线程。
3. 保存后仍从 Solution 右键构建，不把单独 CPU0 Build 当最终验收。
4. 重新运行校验脚本，特别检查生成的 `pin_data.c`、`bsp_clock_cfg.h`、`hal_data.c`。

### 10.3 修改 CPU1 服务

CPU1 可以添加不触碰功率实时链的完整性、日志和存储任务。优先使用静态任务栈，明确周期、最坏执行时间和 IPC 带宽。仍从 Solution 右键构建整链。

## 11. UART9/SCICLK 错误的真实诊断方法

本工程出现过 CPU0 配置页报 `g_uart9 Requires SCISPICLK/SCICLK`。真实根因不是 UART9 引脚，而是 Solution 的 `solution.xml` 已正确、隐藏 `.secure_xml` 缓存仍把 PLL2/SCICLK 关着，导致错误时钟进入 Solution `.sbd`，进而生成 `BRR=0`。

正确判断顺序：

1. `solution.xml`：`board.clock.pll2.source.xtal`、`board.clock.sciclk.source.pll2r`；
2. Solution `.secure_xml`：同样的两项必须一致；
3. Solution `.sbd` 内 `cfg/secure.xml`：同样一致；
4. CPU0 `ra_gen/bsp_clock_cfg.h`：PLL2 来源为 MAIN_OSC，SCICLK 来源为 PLL2R；
5. CPU0 `ra_gen/hal_data.c`：UART9 为 `BRR=64`，计算误差 0.160%。

任何一级不一致都不能把 Problems 标记当“假阳性”删除。修复上游后从 Solution 完整重建，再关闭并重新打开配置编辑器，让 e² studio 重新验证。

## 12. 双核下载与调试

CPU1 工程包含 `RA8P1_DAB_DualCore_Solution_CPU1 Debug_Multicore.launch`。当前 e² studio 生成的文件是一个同时启用 CPU0/CPU1 的 Multicore 调试配置：它显式加入 CPU0 ELF，并以 CPU1 工程 ELF 作为当前程序。Renesas 文档把这种入口描述为 Multicore Launch Group；实际文件类型和界面名称以所用 e² studio 版本为准。

1. 第一次必须断开功率级或保持功率路径物理失能。
2. 先完成整链 Build 和校验脚本。
3. 打开 `Run > Debug Configurations`。
4. 选择 CPU1 工程中的 `...CPU1 Debug_Multicore`，而不是只选 CPU0 的单核启动项。
5. 启动后在 Debug 视图确认两个会话都出现。
6. CPU0 观察 `g_dab_cpu0_dual_core_status`，CPU1 观察 `g_cpu1_service_status`。
7. 先验证两边 `peer_seen` 和收发计数增长，再做 UART9 文本回归。

调试一个核停在断点时，另一个核可能继续运行。涉及功率外设时，不能依赖“调试器暂停”作为安全措施。

只编译通过不能证明：

- CPU1 镜像已被实际下载；
- 两核心跳在板上稳定；
- UART 文本、PWM、ADC/DMAC 波形与参考工程物理一致；
- SDRAM 时序正确；
- 有功率运行安全。

当前工程没有写入永久锁、密钥或 OTP 的应用代码。普通调试下载本身也不应被描述为“绝对不会影响以后烧录”；换回单核工程时，按正常调试器流程重新下载，必要时执行受控的全片擦除即可。任何擦除都应先确认目标芯片和需要保留的数据。

## 13. 软件保护和测试模式

CPU0 的 `dab_first_power_config.h` 默认：

```c
#define DAB_SW_PROTECTION_PROFILE DAB_SW_PROTECTION_PROFILE_REFERENCE_TEST
#define DAB_MANUAL_STOP_TEST_MODE 1U
#define DAB_POEG_PWM_SHUTDOWN_ENABLE 0U
```

该档位与参考测试固件当前语义一致，五项应用软件自动保护默认关闭：

- `DAB_SOFTWARE_AUTO_TRIP_ENABLE`
- `DAB_GPIO_OC_TRIP_ENABLE`
- `DAB_ADC_CURRENT_GUARDS_ENABLE`
- `DAB_VIN_UV_TRIP_ENABLE`
- `DAB_NO_RISE_TRIP_ENABLE`

当前编译锁要求上述五项软件保护和 POEG 关断全部为 0。`on` 成功后，ADC/GPIO/POEG 告警和连续 PWM 更新失败都不会由应用软件触发关断；PWM 保持最后一次成功写入的相移，只有显式 `off` 或诊断停止结束输出。真正的外设初始化失败仍会报告启动失败，不能伪装成已经发波。

全部测试结束后若要恢复保护，先把 `DAB_MANUAL_STOP_TEST_MODE` 改为 0，再把档位切换为 `DAB_SW_PROTECTION_PROFILE_ALL_ENABLED` 或逐项覆盖。若还要恢复 POEG 异步钳位，必须同时把 `DAB_POEG_PWM_SHUTDOWN_ENABLE` 改为 1，并在 CPU0 FSP 中恢复 GPT output-disable/GTIOC disable 设置后从 Solution 重新生成构建。不要把这些宏加到 Solution 或 CPU1。

这些宏不能代替限流电源、急停、示波器和独立表计。测试模式不加载 AI 模型，并保留原 UART 命令、`ph` 实时相移、PWM/ADC 所有权和功率控制效果。

## 14. 有效数据和调参记录

参考测试工程的 `DAB_Live_Test_Records` 是唯一的整理后数据目录：同一电压/负载/功率工况只对应一个 CSV，重复测试追加并以 `source_file + sequence` 去重。`INDEX.csv` 给出样本数和统计，`CONDITION_MAP.csv` 保存来源归属，`TUNING_NOTES.md` 保存外部表计和可复用结论；零散串口日志不得复制到这里。

训练样本必须同时满足 `control_on=1`、`pwm_on=1`、`fault=0`、`adc_valid=1`、状态为 SOFTSTART/RUN、相移和输入/输出功率均为正，并且明确映射到工况。文件名带 `FW` 表示电压/功率只有固件 ADC/计算来源，不是独立仪表真值。2026-08-11 的 PWM 期间 ADC 受共模干扰，虽有外部电压证明确实传功，也不进入训练 CSV。

以后完成一次采集后，给 `CONDITION_MAP.csv` 增加映射：相同工况复用旧 `condition_file`，新工况才起新文件名；再运行 `tools/Consolidate-DabValidPowerData.ps1`。脚本遇到有效但未映射的行会报错，不会悄悄丢弃或自动制造一堆文件。

## 15. 常见问题速查

| 现象 | 原因/检查 |
| --- | --- |
| 选中 Solution 后锤子是灰色 | Solution 是容器，不是普通 CDT 工程；右键 Solution 选 `Build Project` |
| CPU0/CPU1 XML 似乎没有 Clocks | Flat 子工程应从 Solution 继承；检查 Solution、`.secure_xml` 和 `.sbd` |
| CPU1 Pins 页出现叉号或 26 条 `configured in preceding project` | 这些提示的资源是 CPU1 `configuration.xml`，表示相关物理引脚已由前序 CPU0 Smart Bundle 占用，是跨核仲裁而非编译错误；本工程的验收条件是 CPU1 `pincfg active=false`、无 `r_ioport`、隐藏引脚缓存为 0、`pin_data.c` 不含 `BSP_IO_PORT_*`，并且启动代码不调用 `R_IOPORT_Open()`。不要为消除提示而让 CPU1 重新生成引脚。 |
| UART9 报缺 SCICLK | 不要删标记；逐级检查 PLL2/SCICLK、`.sbd`、`bsp_clock_cfg.h`、BRR |
| 只构建 CPU1 失败 | CPU1 依赖 CPU0 `.sbd`，CPU0 又依赖 Solution `.sbd`；从 Solution 构建 |
| 只看到 CPU0 在跑 | 确认 CPU0 已执行 `R_BSP_SecondaryCoreStart()`，并使用 Multicore Launch Group |
| 两个 RTOS 能否共用队列 | 不能直接共用；使用 IPC，或设计共享内存协议与 Cache 维护 |
| SDRAM 状态是 disabled | 当前故意关闭；先确认硬件和 FSP 时序，不要只改应用宏 |
| CRC 通过是否代表安全启动 | 不代表；CRC 只检错，安全启动需要 RSIP/签名/TrustZone 方案 |
| 编译 0 错误能否直接上功率 | 不能；先做无功率下载、双核心跳、串口、PWM/ADC 波形回归 |

## 16. 官方资料

- [FSP 多核工程创建、Solution 构建与 Launch Group](https://renesas.github.io/fsp/_s_t_a_r_t__d_e_v.html)
- [RA8P1 产品功能总览](https://www.renesas.com/en/products/ra8p1)
- [RA8P1 Group Datasheet](https://www.renesas.com/en/document/dst/ra8p1-group-datasheet)
- [RA8P1 Group User's Manual: Hardware](https://www.renesas.com/en/document/mah/ra8p1-group-users-manual-hardware)
- [FSP RA8P1 支持模块列表](https://renesas.github.io/fsp/group___b_s_p___m_c_u___r_a8_p1.html)
- [FSP IPC 驱动](https://renesas.github.io/fsp/group___i_p_c.html)
- [FSP CRC 驱动](https://renesas.github.io/fsp/group___c_r_c.html)
- [e² studio 2025-12 引脚传播规避说明](https://renesas.github.io/fsp/_e2__s_t_u_d_i_o__p_i_n__c_o_n_f_i_g_u_r_a_t_i_o_n__w_o_r_k_a_r_o_u_n_d_s.html)
