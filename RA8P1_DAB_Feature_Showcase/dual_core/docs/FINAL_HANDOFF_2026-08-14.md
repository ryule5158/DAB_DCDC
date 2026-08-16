# RA8P1 DAB 双核工程最终交付记录

> 2026-08-15 更正：2026-08-14 的验收漏掉了 e² studio 工作区资源数据库中的幽灵 `Debug/r_ioport` 记录，导致从 Solution 构建时在 Makefile Generator 阶段失败。该问题已复现并修复；15:35 已按“Solution 根节点 → 项目 → 构建项目”完成 CPU0、CPU1 构建，15:40 又从 Solution 工程右键菜单完成增量构建，两次均为 0 个错误且未再次出现 Restart Editor。修复证据见 `GUI_BUILD_RECOVERY_2026-08-15.md`。本文件以下内容保留为 2026-08-14 的交付快照。

完成时间：2026-08-14 21:48（Asia/Shanghai）

## 交付对象

只修改了克隆工程：

`C:\Users\LENOVO\Desktop\Electric Simulation\RA8P1_DAB_Feature_Showcase\dual_core`

长期测试工程 `C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1` 始终作为只读参考。本次没有操作 COM4、VOFA+、烧录器或功率硬件。

## 最终结果

- CPU0 是 Cortex-M85，保留参考测试工程 v32 的全部 DAB 实时功能、UART9 协议、`ph 0..480`/`ph auto` 命令和默认 manual-stop-only 测试语义。
- CPU0 负责 PWM、ADC/DMAC、控制、保护决策、UART9 和全部板级引脚；它在安全初始化后启动 CPU1，并通过独立 FreeRTOS IPC 任务交换数据。
- CPU1 是 Cortex-M33，运行独立 FreeRTOS，包含 IPC 服务、硬件 CRC 完整性任务和受宏控制的 SDRAM 状态任务；它不写 PWM、ADC、UART、保护决策或任何板级引脚。
- CPU1 Pins 的 `Generate data` 关闭，公开和隐藏活动引脚均为 0；没有 `r_ioport` FSP 实例、启动调用、链接对象或生成引脚表。
- CPU0 测量路径已实际生成 Helium/MVE-F 指令；热点代码/状态进入 TCM，DMA 环形缓冲区进入 non-cache SRAM。
- 测试模式不包含 AI 模型。SDRAM 驱动和快速测试已经实现，但在实板型号、容量、引脚及时序确认前保持默认关闭。

## 构建与自动验收

使用 e² studio 2025-12 / FSP 6.4.0 随附的 LLVM 21.1.1 和 GNU Make 4.3，按 CPU0→CPU1 顺序完成重新构建：

| 工程 | 结果 | text | data | bss |
| --- | --- | ---: | ---: | ---: |
| CPU0 | 0 error | 64678 | 44 | 21093 |
| CPU1 | 0 error | 11766 | 92 | 6496 |

最终命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Verify-ReferenceParity.ps1 -RequireBuildArtifacts
```

全部检查通过，包括 XML 可解析、v32 源码/FSP/引脚/时钟/UART9 等价、双核构建链、两个 ELF、IPC/CRC/SDRAM、真实 Helium 指令、TCM/non-cache 放置以及 CPU1 无引脚和无残留 `r_ioport` 构建项。

CPU0 完整重编译会显示 CMSIS-DSP signedness 和原 PWM 枚举按位组合的既有警告；没有编译或链接错误。FSP 编辑器 Problems 中的警告数量不能代替配置检查，当前 Pins 页本身无红叉，CPU1 无引脚所有权由 XML、生成 C 文件、构建清单和自动验收共同证明。

## e² studio 使用入口

工作区应同时包含以下三个工程：

1. `RA8P1_DAB_DualCore_Solution`
2. `RA8P1_DAB_DualCore_Solution_CPU0`
3. `RA8P1_DAB_DualCore_Solution_CPU1`

构建时在 Project Explorer 中右键 `RA8P1_DAB_DualCore_Solution`，选择 `Build Project`。Solution 是容器/依赖入口，顶部工具栏锤子在选中 Solution 时可能为灰色；这不等于工程不可构建。调试使用 CPU1 工程内的 `Debug_Multicore.launch`，其中同时加载 CPU0 和 CPU1 ELF。

详细操作见 `E2STUDIO_DUAL_CORE_GUIDE_ZH.md`，架构、RTOS、IPC、CRC、Helium、SDRAM、内存和保护边界见 `RA8P1_DUAL_CORE_AND_FEATURE_DEVELOPMENT_GUIDE_ZH.md`。

## 尚未由本次工作证明

本地构建和静态校验不能证明烧录后一定没有硬件问题。本次没有烧录，也没有宣称完成双核启动、IPC 心跳、UART 文本、PWM/ADC 波形或带功率验证。第一次上板仍应在断开功率级或限流低压条件下，依次验证双核 Launch、IPC、UART9、停机输出和波形。

当前 v32 为严格匹配参考测试固件，GPT0/1/5/8 的 POEG 输出钳位是 `GPT_GTIOC_DISABLE_PROHIBITED`；软件保护档位不会自动恢复该硬件配置。需要硬件 POEG 异步钳位时，必须另行修改 CPU0 FSP 配置、重新生成并完成独立验证。
