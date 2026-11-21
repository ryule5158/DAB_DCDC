# RA8P1 双核工程暂停工作记录

保存时间：2026-08-14 20:57（Asia/Shanghai）

> 这是保留用于追溯的暂停快照，里面的“尚未完成”不再代表当前状态；任务已于 2026-08-14 21:48 收口，最终状态见 `FINAL_HANDOFF_2026-08-14.md`。

## 任务边界

- 只允许修改克隆工程：`C:\Users\LENOVO\Desktop\Electric Simulation\RA8P1_DAB_Feature_Showcase\dual_core`
- 测试基准工程：`C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1`，保持只读；用户正在另一个任务中继续修改和实测它。
- 未操作 COM4、VOFA+、烧录器或功率硬件；当前证据仅限配置、生成代码、静态核对和本机构建。

## 已完成的实质修复

- Solution 只持有共享 SWD 引脚 P210/P211，不再占用四线 JTAG 的 P208/P209。
- CPU0 独占全部板级、UART9、IIC、PWM、ADC、POEG 和未来 SDRAM 的全局引脚/初始化。
- CPU1 的 Pins `Generate data` 已关闭，`pincfg active=false`，活动引脚数为 0。
- CPU1 配置中已移除 `r_ioport` 组件、模块、栈和配置；启动代码不再调用 `R_IOPORT_Open()` 或 `R_BSP_SdramInit()`。
- CPU1 `ra_gen/pin_data.c` 不含任何 `BSP_IO_PORT_*`；`common_data.c/.h` 不再引用 `g_ioport` 或 `g_bsp_pin_cfg`。
- CPU1 隐藏 `.secure_xml` 中的本地/有效引脚设置数为 0，这是当前无引脚所有权架构的正确状态。
- 已同步过一次测试工程的 POEG 有界重试修改，同时保留双核启动、保护档位和 stdout/Helium 适配层。

## 最近一次成功构建

- CPU0：LLVM/Make 直接构建成功；text 62978、data 44、bss 21085；8 个既有编译警告，无错误。
- CPU1：LLVM/Make 直接构建成功；text 11766、data 92、bss 6496；0 diagnostics。
- 这些是本机构建结果，不代表已经烧录或通过实板验证。

## 暂停时尚未完成

1. `tools/Verify-ReferenceParity.ps1` 第 226 行仍是旧断言：要求 CPU1 隐藏引脚缓存等于 CPU0。应改为断言 CPU1 缓存条目数为 0。
2. README 和三份中文文档仍有“CPU1 隐藏缓存继承 CPU0”的旧说明，应统一改成“CPU1 关闭引脚生成、无 r_ioport、隐藏缓存为空”。
3. 用户的另一个任务在 20:48--20:51 又修改了基准工程的四个文件：
   - `src/dab_first_power_config.h`
   - `src/dab_pwm_hw.c`
   - `src/dab_pwm_hw.h`
   - `src/User_Init_Task_entry.c`
   因基准仍在变化，尚未做最后一次同步。
4. 待基准稳定后，应只把上述业务变化同步到克隆 CPU0，同时保留 `dab_dual_core_cpu0.h`、`dab_dual_core_cpu0_start()`、保护档位适配和已有双核/Helium 适配。
5. 同步后重新构建 CPU0/CPU1，运行：
   `powershell -ExecutionPolicy Bypass -File tools/Verify-ReferenceParity.ps1 -RequireBuildArtifacts`
6. 最后在用户不再操作基准 e² studio 时，从克隆 Solution 重新 Generate/Build，核对两个 Console 和 Problems；仍不得烧录。

## 构建工具位置

- LLVM：`C:\Renesas\RA\e2studio_v2025-12_fsp_v6.4.0\toolchains\llvm_arm\ATfE-21.1.1-Windows-x86_64\bin`
- Make：`C:\Renesas\RA\e2studio_v2025-12_fsp_v6.4.0\eclipse\plugins\com.renesas.ide.exttools.gnumake.win32.x86_64_4.3.1.v20240909-0854\mk\make.exe`

## 恢复任务时的第一步

先读取本文件，再只读检查上述四个基准文件的时间戳/哈希是否已稳定；不要直接操作 GUI，也不要假定基准已停止变化。
