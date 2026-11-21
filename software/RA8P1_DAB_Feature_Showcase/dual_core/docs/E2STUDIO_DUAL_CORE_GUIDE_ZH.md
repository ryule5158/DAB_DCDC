# e² studio 中的 RA8P1 双核开发

## 1. 与单核工程最本质的区别

单核工程通常只有一份 `configuration.xml`、一个链接脚本、一个 ELF 和一个调试会话。RA8P1 Multicore Flat 工程由一条工程链组成：

```text
Solution（全局资源）
  └─ CPU0 / Cortex-M85（主核镜像）
       └─ CPU1 / Cortex-M33（副核镜像）
```

三个工程都要导入同一个 e² workspace。Solution 不承载业务 `main()`，它负责全局时钟、引脚、Flash/RAM/SDRAM 分区和跨核依赖；CPU0、CPU1 各有自己的 FSP 配置、FreeRTOS、链接过程和 ELF。

本工程的分区为：

| 资源 | CPU0 | CPU1 |
| --- | --- | --- |
| Code Flash | `0x00000000` 起，`0x80000` 字节 | `0x00080000` 起，`0x80000` 字节 |
| 片上 RAM | `0x00000000` 起，`0xEA000` 字节 | `0x000EA000` 起，`0xEA000` 字节 |
| SDRAM 预留 | 前 64 MiB | 后 64 MiB，CPU1 基址 `0x6C000000` |
| 紧耦合内存 | ITCM/DTCM | CTCM/STCM |

SDRAM 的“分区存在”不等于实板外部存储器已经配置或验证；本工程默认禁止访问。

## 2. 打开现有工程

1. 在 e² studio 选择一个干净 workspace。
2. `File > Import > Existing Projects into Workspace`。
3. 根目录选择本 `dual_core` 目录。
4. 同时勾选 `RA8P1_DAB_DualCore_Solution`、`..._CPU0` 和 `..._CPU1`，不要勾选 Copy projects into workspace。
5. 确认 Project Explorer 中三个工程均已打开，Solution 位于依赖链最上游。

`.metadata` 是本机 workspace 缓存，不属于三个源工程。当前 e² studio 正以本目录作为 workspace 时它会存在；不要在 IDE 运行期间删除，也不要把它作为源代码提交。

## 3. 配置应该改在哪里

- 全局时钟、Flash/RAM/SDRAM 分区：改 `RA8P1_DAB_DualCore_Solution/solution.xml`。
- CPU0 的外设、线程和业务代码：改 CPU0 工程。
- CPU1 的外设、线程和后台服务：改 CPU1 工程。
- 同一外设只能明确归一个核所有。不要让 CPU1 再初始化 CPU0 已使用的 GPT、ADC、DMAC、POEG 或 UART9。
- 跨核状态通过 IPC 消息传递；若以后共享带缓存的 RAM 缓冲区，必须同时设计所有权、内存屏障和 Cache clean/invalidate，不能只共享一个裸指针。

本工程选择 CPU0 独占完整 DAB 实时链，CPU1 只处理慢速服务，因此不会改变测试模式的控制时序和串口效果。

软件保护档位在 CPU0 的 `src/dab_first_power_config.h` 中选择。当前测试镜像固定 `DAB_MANUAL_STOP_TEST_MODE=1`、`DAB_POEG_PWM_SHUTDOWN_ENABLE=0` 和 `REFERENCE_TEST`：五项软件保护及 POEG 驱动关断都关闭，`on` 成功后只有显式 `off`/诊断停止会关 PWM。编译期检查会拒绝手动停机模式与任一自动关断同时启用。全部测试结束后，先把手动停机模式改为 0，再选择保护档位；不要把这些宏加到 Solution 或 CPU1。四路 GPT 当前还独立配置为 `GPT_OUTPUT_DISABLE_NONE`/`GPT_GTIOC_DISABLE_PROHIBITED`。

## 4. 生成和构建

每次修改配置后：

1. 保存相关配置编辑器。
2. 在 Project Explorer 右键 `RA8P1_DAB_DualCore_Solution`。
3. 选择 `Build Project`（“构建项目”）。Solution 是容器工程，顶部锤子按钮灰色并不表示工程坏了。
4. 等待依赖链依次完成 CPU0、CPU1 构建。
5. 检查 CPU0/CPU1 Console 均为 0 errors，并运行 `tools/Verify-ReferenceParity.ps1 -RequireBuildArtifacts`。

不要只对 CPU1 点 Build，也不要在 CPU0/CPU1 配置页分别随意 Generate 后就认为全局设置已传播。Renesas 的 FSP 文档要求多核 Solution 从 Solution 工程触发构建，以便向下游传播配置。

2026-08-15 16:24 已按上述右键入口复测：CPU0、CPU1 Console 均为 `Build Finished. 0 errors, 0 warnings`，没有再次出现 Restart Editor。Problems 中的 26 条 `Pin ... has been configured in preceding project` 全部属于 CPU1 `configuration.xml`，是 CPU1 看到 CPU0 已占用物理引脚后的跨核资源提示，不是构建失败或 CPU1 重复生成引脚。验收时应区分三件事：Console 编译诊断、Problems 的错误数、Solution 的跨核占用提示。

`Solution/build` 和两个 CPU 工程的 `Debug` 虽然属于生成目录，但其中的 Smart Bundle（`.sbd`）和 `bsp_linker_info.h` 是图形化配置编辑器及下一级工程的输入，不能在“整理工程”后直接留空。依赖链为：

```text
Solution.sbd -> CPU0.sbd + CPU0 bsp_linker_info.h
             -> CPU1.sbd + CPU1 bsp_linker_info.h
```

如果这些目录被清理，按 `Solution -> CPU0 -> CPU1` 顺序重新构建；每一级成功后再构建下一级，最后关闭并重开 CPU0/CPU1 的 `configuration.xml`。本机工程把编译并行度固定为 4。当前应用不调用 CMSIS-DSP，所以它已从两个 FSP stack、库目录和 CPU0 残留 include 路径中移除；CPU0 的 Helium 直接使用 MVE intrinsic，不受影响。`-RequireBuildArtifacts` 会同时检查三个 `.sbd`、两个 `bsp_linker_info.h` 和两个 ELF，整理后必须通过该检查。

本机使用 e² studio 2025-12 + FSP 6.4.0。Renesas 已记录该版本在三级以上 project chain 中可能发生引脚传播问题，Multicore Flat 正好是 Solution + CPU0 + CPU1 三级链。因此每次重新生成后都必须检查：

- CPU0 `ra_gen/pin_data.c`
- CPU1 `ra_gen/pin_data.c` 必须不含任何 `BSP_IO_PORT_*`；CPU1 不拥有外部引脚
- CPU0 `ra_gen/bsp_clock_cfg.h`
- CPU0 `ra_gen/hal_data.c` 中 UART9 波特率
- 等价核对脚本的 168 项有效引脚、103 项时钟和 1051 项 FSP 属性

本工程的引脚所有权是固定设计：Solution 只配置 P210/P211 的 SWD，CPU0 配置全部板级引脚；CPU1 关闭 Pins 的 `Generate data`，公开 `pincfg` 为非活动状态，且配置中没有 `r_ioport`。CPU1 的隐藏 `.secure_xml` 不保存本地或有效引脚，`ra_gen/pin_data.c` 也不得包含任何 `BSP_IO_PORT_*`。核对脚本会同时检查公开 XML、隐藏缓存、FSP 组件和生成 C 文件，不能只看 Pins 页图标。

全局时钟只由 Solution 拥有；CPU0、CPU1 的 Flat 子工程均不保存重复时钟块。103 项时钟必须同时在 `solution.xml`、Solution 隐藏 `.secure_xml` 和 Solution `.sbd` 的 `cfg/secure.xml` 中保持一致。最终生成结果必须是 `PLL2=XTAL`、`SCICLK=PLL2R`，CPU0 UART9 的 115200 计算误差为 0.160%、BRR 为 64。

如果 CPU0 显示 “Requires SCISPICLK/SCICLK”，不能把它当假阳性删除。本工程曾出现 `solution.xml` 正确、隐藏 `.secure_xml` 仍关闭 PLL2/SCICLK 的真实缓存分叉，导致下游生成 BRR=0。修复上游并从 Solution 完整重建后，应关闭并重新打开配置编辑器。详细逐级检查见 [RA8P1 DAB 双核与特色功能开发手册](RA8P1_DUAL_CORE_AND_FEATURE_DEVELOPMENT_GUIDE_ZH.md)。

## 5. CPU1 是怎样启动的

RA8P1 复位后先运行 CPU0。CPU1 不会因为“有一个 CPU1 工程”就自动执行。

本工程在 CPU0 完成 `dab_board_initialize_safe()` 或外设测试的 `test_force_safe_outputs()` 后调用：

```c
R_BSP_SecondaryCoreStart();
```

随后 CPU1 从 Solution 指定的 CPU1 Flash 分区和向量表启动。把 CPU0 断点停在这句之前时，CPU1 尚未运行，CPU1 调试器也可能无法正常连接；这是双核初学时最常遇到的现象之一。

## 6. 双核调试

CPU1 工程包含 `RA8P1_DAB_DualCore_Solution_CPU1 Debug_Multicore.launch`。在当前 e² studio 版本中，它是同时启用 CPU0/CPU1、显式加入 CPU0 ELF并使用 CPU1 工程 ELF 的 Multicore 调试配置；Renesas 文档将这一入口称为 Launch Group。

1. 保持功率级断开，第一次仅做无功率调试。
2. 在 Debug Configurations 中选择 CPU1 工程的 Multicore Launch Group。
3. 点击 Debug；e² 会先下载/连接 CPU0，再在 CPU0 启动副核后连接 CPU1。
4. Debug 视图中会出现两个调试会话和各自的调用栈。
5. CPU0 看 `g_dab_cpu0_dual_core_status`；CPU1 看 `g_cpu1_service_status`。IPC 的 `peer_seen`、收发计数和 CRC 状态是第一组应观察的变量。

只启动 CPU0 的 `Debug_Flat.launch` 只会下载并连接 CPU0，不能证明 CPU1 已烧录或运行。

调试注意事项：

- 单步或断住一个核时，另一个核通常仍会运行；观察外设时不要默认“双核一起暂停”。
- CPU0 负责功率级，任何有源测试仍须保留硬件急停、限流电源和示波器。软件保护关闭不等于硬件保护关闭。
- 先在无功率状态证明 CPU1 心跳，再做串口回归，最后才考虑受控功率测试。

## 7. 本工程的 FreeRTOS 任务

| 核 | 任务 | 周期 | 职责 |
| --- | --- | --- | --- |
| CPU0 | 原测试工程任务 | 原配置 | DAB 控制、命令、诊断 |
| CPU0 | `CPU0 IPC` | 10 ms、空闲优先级 | IPC 收发和对端状态；不抢占优先级 1 的原 DAB 任务 |
| CPU1 | `new_thread0` | 10 ms | IPC 服务与心跳 |
| CPU1 | `CPU1 Integrity` | 1 s | 硬件 CRC32、自检和状态上报 |
| CPU1 | `CPU1 Memory` | 5 s | SDRAM 状态与可选快速测试 |

这体现了 RTOS 的并行职责分离，但没有为了“看起来像双核”而拆开对时序敏感的 DAB 控制链。

## 8. 实测数据如何保存

有效数据统一放在参考测试工程的 `DAB_Live_Test_Records`。同一电压/负载/功率工况只保留一个 CSV，后续重复测试追加并去重；零散串口日志不得复制到该目录。只有 `control_on=1`、`pwm_on=1`、零故障、ADC 有效、RUN/SOFTSTART、正相移且正功率的已映射行才进入训练数据。文件名前缀 `FW` 表示缺少独立仪表标签，不能当作效率真值。转换工具为 `tools/Consolidate-DabValidPowerData.ps1`，具体规则见数据目录自身的 `README.md`。

## 9. 官方参考

- [FSP：Starting Development of Your Application](https://renesas.github.io/fsp/_s_t_a_r_t__d_e_v.html)
- [Renesas：Multicore Setup and Running Hello World](https://www.renesas.com/en/document/apn/multicore-setup-and-running-hello-world-dual-core?language=en)
- [FSP：e² studio Pin Configuration Workarounds](https://renesas.github.io/fsp/_e2__s_t_u_d_i_o__p_i_n__c_o_n_f_i_g_u_r_a_t_i_o_n__w_o_r_k_a_r_o_u_n_d_s.html)
- [FSP：RA8P1 BSP Support](https://renesas.github.io/fsp/group___b_s_p___m_c_u___r_a8_p1.html)
