# RA8P1 DAB 双核工程最终整理记录（2026-08-15）

## 当前基线

- 参考工程：`C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1`。
- 参考 Git HEAD：`33023979466aea613271bdab748e99ec1ef8d137`；对照时以该目录 2026-08-14 的当前工作树为准，因为实机测试代码比 HEAD 更新。
- 双核工程：本 `dual_core` 目录中的 Solution、CPU0、CPU1 三工程。
- CPU0 固件版本：`2026-08-15-dualcore-manual-stop-v33`。

CPU0 保留参考工程全部 DAB 业务源码、UART9 命令、PWM、ADC-B/DMAC、测量、控制和诊断。白名单差异只有双核启动/IPC、stdio 适配、Helium 向量路径、手动停机编译锁和 POEG 测试旁路。CPU1 不接管功率实时链，只运行 IPC、硬件 CRC 和默认关闭的 SDRAM 服务。

## 测试模式定稿

- `DAB_MANUAL_STOP_TEST_MODE=1`。
- 五项应用软件自动保护全部为 0。
- `DAB_POEG_PWM_SHUTDOWN_ENABLE=0`；`R_POEG_Open/Reset/StatusGet` 在当前镜像中被编译掉。
- GPT0/1/5/8 均为 `GPT_OUTPUT_DISABLE_NONE` 和 `GPT_GTIOC_DISABLE_PROHIBITED`。
- 连续 PWM 寄存器更新失败只记录一次告警并保持最后一次成功相移，不再无条件调用 `dab_fault_latch()`。
- `on` 成功进入 PWM 后只接受显式 `off`/诊断停止。真正的 GPT/ADC/UART 初始化失败仍会报告错误，不能伪装成已经发波。
- 不加载 AI 模型。

## RA8P1 特色实际使用

- 双核：M85/CPU0 执行 DAB；M33/CPU1 与其真正并行执行后台任务。
- 两套 FreeRTOS：每核独立调度器、任务栈和中断上下文；不是 SMP。
- IPC FIFO：两核 10 ms 服务和心跳；CPU0 IPC 使用空闲优先级，不抢占参考 DAB 任务。
- CRC：CPU1 使用 RA8P1 硬件 CRC32，含固定向量自检。
- Helium/MVE-F：CPU0 测量目标文件含真实 `vldrh/vmovlb/vcvt/vfma` 向量指令。
- TCM/Cache：热点代码/状态位于 ITCM/DTCM，DMA ring 位于 non-cache SRAM。
- SDRAM：分区、驱动、分配器和快速测试存在，但 `DAB_CPU1_SDRAM_ENABLE=0`；未确认板级时序前不访问。
- CMSIS-DSP：应用无调用，已从两个 FSP stack、库目录和 CPU0 残留 include 路径移除；Helium 不依赖它。

## 构建和静态验证

2026-08-15 使用 e² studio 2025-12/FSP 6.4.0 自带 LLVM 21.1.1 与 GNU Make 4.3 重建：

- CPU0：0 errors、0 warnings；text 64414、data 44、bss 21069 字节。
- CPU1：0 errors、0 warnings；text 11766、data 92、bss 6496 字节。
- `tools/Verify-ReferenceParity.ps1 -RequireBuildArtifacts`：全部通过。
- 16:24 从 Project Explorer 的 Solution 根节点右键执行 `Build Project` 再次通过：CPU0、CPU1 控制台均为 `Build Finished. 0 errors, 0 warnings`，没有 Restart Editor 弹窗，Problems 为 `0 errors`。

Problems 中仍列出 26 条 `Pin ... has been configured in preceding project`。逐项展开并双击确认后，它们的资源是 CPU1 `configuration.xml`：这是 CPU1 从 CPU0 Smart Bundle 看到“引脚已由前序 CPU0 工程占用”的跨核资源仲裁提示，不是 CPU1 实际拥有这些引脚，也不是编译警告。保留这些提示可防止 CPU1 抢占 CPU0 的 ADC、PWM/UART/板级引脚；强行清零反而会破坏 Solution 的引脚所有权检查。

验证覆盖：正式/隐藏 XML、Solution/CPU Smart Bundle、1051 项 FSP 模块属性、CPU0/CPU1/Solution 引脚所有权、103 项时钟、UART9 BRR、Option Function 设置、Flash/RAM/SDRAM 非重叠分区、CPU1 无 IOPORT、CMSIS-DSP 移除、POEG 旁路、手动停机锁、IPC/CRC/SDRAM/Helium/TCM 和 ELF 时间顺序。

以上只证明源码、配置和构建一致性，尚未在本轮下载芯片，也没有宣称双核镜像已经通过实板 UART/PWM/ADC/功率回归。第一次下载仍应断开功率级，使用 CPU1 工程的 Multicore Launch，先确认两核、UART 和无功率 PWM/ADC，再按原测试流程上功率。

两个核的 `bsp_mcu_ofs_cfg.h` 与参考工程逐字节一致，应用代码不写永久锁、密钥或 OTP；这排除了本次源码主动锁芯片的新增路径，但任何实际下载风险仍必须由正确目标、调试配置和首次无功率验证控制。

## 数据整理

专用目录：`C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1\DAB_Live_Test_Records`。

- 7 个按工况合并的 CSV，共 4743 个有效传功样本。
- 同一工况以后追加到同名 CSV；`INDEX.csv` 统一统计。
- 文件名前缀 `FW` 表示没有独立仪表标签，不能当作效率真值。
- 2026-08-11 的固件 ADC 数据因 PWM 共模干扰被全部排除，仅在调参记录中保留外部电压事实。
- 转换脚本会拒绝有效但未映射的新样本，避免静默丢数据或继续生成一堆无含义日志。

## 以后需要人工操作的边界

工程编辑、数据整理、命令行构建和静态核对已经不需要人工操作。下一次必须由操作者在场的是首次双核下载与实板回归，因为这会接触调试器和功率硬件；本轮没有触碰 COM4、VOFA+、下载器或功率级。
