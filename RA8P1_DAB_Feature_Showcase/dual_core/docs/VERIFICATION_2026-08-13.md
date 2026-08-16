# 静态等价与构建验证记录（2026-08-13）

## 结论

CPU0 的默认 `REFERENCE_TEST` 配置与长期测试工程保持相同的 DAB 功能和测试行为；差异只用于启动/服务 CPU1、兼容 LLVM 浮点格式化，以及提供不改变默认值的软件保护档位。CPU1 不拥有功率外设，不改变 CPU0 的命令、保护判断或串口文本。

这是源码、FSP 配置、生成代码、构建和反汇编证据，不是烧录、板上双核心跳、串口回归、PWM/ADC 波形或有功率验证。

## 与参考工程的核对

参考工程：`C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1`（核对期间只读）。

- CPU0 包含参考工程的全部 `src` 文件。
- 除五个已记录适配点外，其余共同源文件 SHA-256 相同。
- `User_Init_Task_entry.c`：只在安全输出初始化后启动 CPU1。
- `ra8p1_dab_peripheral_test.c`：只在测试安全输出建立后启动 CPU1。
- `dab_measurements.c`、`dab_ra8p1_scope_test.c`：只把 `printf` 路由到 LLVM 可用的 UART 浮点格式化层，输出格式字符串保持不变。
- `dab_first_power_config.h`：默认档位仍把五项应用软件保护置 0；增加全开档位和五项独立编译覆盖。
- FSP 模块属性：1051/1051 相同。
- 有效引脚配置：168/168 相同；忽略不生成引脚代码的 CANFD0/RMII1 `free pairing` 标记。
- Solution 时钟节点：103/103 相同。
- `pin_data.c`、`common_data.c`、`hal_data.c` 去除空白后逐字符相同。
- CPU0 `bsp_clock_cfg.h` 与参考工程文本相同（允许 CRLF/LF 差异）。
- 生成结果：PLL2 源为 XTAL，SCICLK 源为 PLL2R；UART9 为 115200、计算误差 0.160%。

自动核对命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Verify-ReferenceParity.ps1 -RequireBuildArtifacts
```

本次所有检查均通过。

## 2026-08-14 e² studio 恢复复核

- 修正 Solution 隐藏 `.secure_xml` 中错误的 PLL2/SCICLK 缓存，并从 Solution 右键执行完整构建；新的 Solution `.sbd` 已把正确时钟传播到 CPU0、CPU1。
- CPU0/CPU1 的 LLVM 并行编译上限固定为 4，完整 CMSIS-DSP 构建不再因一次启动 16 个 Clang 而无诊断退出。
- 2026-08-14 20:11 从 Solution 执行增量构建，CPU0 Console 为 0 error/0 warning，段大小 text 62106、data 44、bss 21085；CPU1 Console 为 0 error/0 warning，段大小 text 12418、data 92、bss 6520。
- 该次 FSP 生成暴露了 CPU1 隐藏 `.secure_xml` 的旧引脚缓存：公开 `configuration.xml` 虽已是 0 项活动引脚，旧缓存仍让 CPU1 `pin_data.c` 重复生成 P514/P515。最终架构改为 CPU1 关闭 Pins 的 `Generate data`、移除 `r_ioport`，隐藏缓存和活动引脚均为 0；Solution 仍只拥有 P210/P211 的 SWD。
- 最终 CPU1 `ra_gen/pin_data.c` 不含任何 `BSP_IO_PORT_*`，`common_data.c/.h` 不引用 `g_ioport`/`g_bsp_pin_cfg`，启动代码不调用 `R_IOPORT_Open()` 或 `R_BSP_SdramInit()`；使用同一 LLVM 21.1.1/Make 构建链重新编译和链接为 0 diagnostics，段大小 text 11766、data 92、bss 6496。CPU1 因而不会在启动时重配 P208/P209、P514/P515 或任何功率级引脚。
- `-RequireBuildArtifacts` 现同时检查三个 `.sbd`、两个 `bsp_linker_info.h`、两个 ELF及其时间顺序；时钟核对同时检查 Solution XML、隐藏 `.secure_xml`、Solution `.sbd`，引脚核对同时检查 CPU1 的引脚生成状态、`r_ioport` 缺席、活动及隐藏缓存为空、最终生成 C 文件和启动代码。
- 2026-08-14 21:41 同步参考工程 v32：CPU0 保留新的 `ph 0..480`/`ph auto` 实时外移相命令、PWM 同步重启与硬件诊断；默认测试档位仍与参考工程一致。
- 随后清理全部对象并按 CPU0→CPU1 重建。CPU0 为 0 error，text 64678、data 44、bss 21093；CPU1 为 0 error，text 11766、data 92、bss 6496。`-RequireBuildArtifacts` 全部通过。
- 2026-08-14 21:48 复现并修复 CPU1 在再次“Generate Project Content”后的增量构建缓存问题：生成器正确删除了无实例的 `r_ioport.c`，但 `.cproject` 链接顺序和 `Debug` 清单仍引用旧 `r_ioport.o`。现已删除该残留并新增两项自动断言；CPU1 重新链接成功，随后完整验收再次全部通过。

## 软件保护档位构建

CPU0 两种档位都使用同一 FSP/LLVM 工程完成链接：

| 档位 | 结果 | ELF 段大小 |
| --- | --- | --- |
| `REFERENCE_TEST`（最终保留，v32） | 0 error | text 64678, data 44, bss 21093 |
| `ALL_ENABLED`（临时切换验证后恢复） | 0 error | text 65248, data 44, bss 21089 |

完整重编译时可能显示来自 CMSIS-DSP signedness 诊断和原 PWM 枚举按位组合的既有编译器警告；它们不是 FSP Pins 冲突，也没有新增链接错误。移除 CPU1 的 `r_ioport` 和全局引脚初始化后，最近一次 CPU1 增量构建为 0 diagnostics，text 11766、data 92、bss 6496。

`REFERENCE_TEST` 是参考测试工程当前的 manual-stop-only 语义：应用层不会因为 ADC 电流、输入欠压、无上升或 GPIO OC 遥测自动关断。当前 v32 为强制 PWM 测试还把 GPT 的 POEG 输出钳位设为 `PROHIBITED`；这是独立 FSP 配置，不受软件保护档位切换影响。栅极驱动器和板上独立硬件保护仍在软件档位之外。

## RA8P1 特性证据

- 双核：正式 Multicore Flat Solution、两个独立 ELF、CPU0 `R_BSP_SecondaryCoreStart()`、双向 IPC 驱动。
- FreeRTOS：CPU0 增加独立 IPC 任务；CPU1 同时运行 IPC、CRC、SDRAM 三种职责。
- 硬件 CRC：CPU1 直接启动 `FSP_IP_CRC` 并访问 `R_CRC->CRCDIR/CRCDOR`，包含已知答案自检。
- SDRAM：CPU1 驱动检查 FSP 使能、控制器状态、容量和对齐，提供保留区分配和破坏性快速测试；因实板信息未确认而默认关闭。
- Helium/MVE-F：`dab_measurements.o` 的反汇编包含 `vldrh.u16 q0`、`vmovlb.u16`、`vmovlt.u16`、`vcvt.f32.u32 q*`、`vmul.f32 q*`、`vldrw.u32 q*`、`vfma.f32 q*`、`vstrw.32 q*`。这证明编译器实际生成向量指令，不只是定义宏。
- 紧耦合内存：Solution 分别为 M85 和 M33 配置 ITCM/DTCM、CTCM/STCM；CPU0 map 已证明控制/保护/PWM/ADC/测量热点进入 ITCM、测量状态进入 DTCM，DMA 环形缓冲区位于 non-cache 系统 SRAM。仍无板上周期测量，不能宣称具体加速比。

## 仍需实板证明的项目

1. Launch Group 同时连接 CPU0/CPU1，IPC 双向心跳稳定。
2. 默认测试模式的 UART 命令、应答文本和状态流与参考工程一致。
3. 无功率状态下八路 PWM、停机状态和 ADC/DMAC 采样一致。
4. 仅在确认硬件后进行限流、有急停的分级功率测试。
5. 确认 SDRAM 物料、总线、时序和信号完整性后再打开 SDRAM 宏。
