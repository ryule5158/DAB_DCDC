# e² studio Solution 构建故障复盘与修复

## 用户看到的现象

从 Project Explorer 右键 `RA8P1_DAB_DualCore_Solution` 执行 `Build Project` 后，配置编辑器提示 Restart；Restart 后构建显示错误。

## 原始错误

旧工作区日志 `.metadata/.bak_3.log` 在 2026-08-14 22:36:29 记录：

```text
在项目“RA8P1_DAB_DualCore_Solution_CPU1”中运行构建器“CDT 构建器”时出错。
资源“/RA8P1_DAB_DualCore_Solution_CPU1/Debug/ra/fsp/src/r_ioport”不存在。
at com.renesas.cdt.managedbuild.llvm.makegen.GnuMakefileGenerator.generateDependencies
```

CPU1 的源工程、FSP 配置和 `.cproject` 已经正确移除了 `r_ioport`，但旧 Eclipse 工作区资源数据库仍保留被删除的 `Debug/ra/fsp/src/r_ioport` 文件夹对象。Renesas Makefile Generator 在构建后扫描这个幽灵资源时抛出 `ResourceException`。这不是 C 编译器错误，也不是 CPU1 又重新取得了引脚所有权。

## 修复

1. 保留旧工作区数据库为 `.metadata_failed_20260814_2236`，用于追溯。
2. 创建全新隔离工作区，从磁盘重新导入 Solution、CPU0、CPU1 三个工程。
3. 使用 e² studio 2025-12 官方 `e2studioc.exe` Managed Build 应用执行真实 Clean Build。
4. 用已验证的新工作区数据库替换活动 `.metadata`。
5. 新增 `Open-RA8P1-DualCore-E2Studio.cmd`，始终用正确的带空格路径启动该工作区。
6. 自动验收新增两项：活动工作区必须登记三个工程；活动 `.metadata/.log` 不得包含 CPU1 幽灵 `Debug/r_ioport` 资源错误。

## 独立工作区构建证据

2026-08-15 使用全新工作区构建同一份磁盘工程：

| 工程 | e² studio 构建结果 | 段大小 |
| --- | --- | --- |
| CPU0 Debug Clean Build | 0 errors，26 个既有编译警告 | text 64678, data 44, bss 21093 |
| CPU1 Debug Clean Build | 0 errors，0 warnings | text 11766, data 92, bss 6496 |

CPU1 Clean/Build 输出的依赖和对象列表均不包含 `r_ioport`。

## 修复后原路径 GUI 复测

2026-08-15 15:35，在修复后的活动工作区中按用户原来的操作路径复测：

1. 在 Project Explorer 选中 `RA8P1_DAB_DualCore_Solution` 根节点。
2. 选择“项目 → 构建项目”（工具栏锤子对容器 Solution 仍可能为灰色）。
3. Solution 依次触发 CPU0、CPU1 的实际构建；本次没有再次出现 Restart Editor 弹窗。
4. e² studio Problems 视图结束时显示 `0 个错误`；构建后活动 `.metadata/.log` 在 15:34 以后没有新增日志项，更没有旧的 CPU1 `Debug/r_ioport` 资源异常。

15:40 又严格复现了用户原入口：在 Project Explorer 的 Solution 行打开右键菜单，点击其中已启用的“构建项目(B)”。双核增量构建再次完成，CPU1 控制台明确显示 `Build Finished. 0 errors, 0 warnings`，仍未出现 Restart Editor 或构建错误。因此“项目”菜单和工程右键菜单两条 GUI 入口均已实测。

16:24 在本轮固件和测试模式改动完成后，再次从 Solution 根节点右键执行同一入口。CPU0、CPU1 控制台都显示 `Build Finished. 0 errors, 0 warnings`，没有 Restart Editor。Problems 显示 `0 errors`；其中 26 条 `Pin ... has been configured in preceding project` 经展开、双击定位后均属于 CPU1 `configuration.xml`，表示 CPU1 看见 CPU0 已占用的引脚。它们是 Solution 的跨核资源仲裁提示，不是 Pins 冲突、串口错误或编译警告。

本次 GUI 构建更新的主要产物：

| 工程 | ELF | S-record | 完成时间 |
| --- | ---: | ---: | --- |
| CPU0 | 2,922,944 bytes | 194,362 bytes | 15:35:20 |
| CPU1 | 515,416 bytes | 35,690 bytes | 15:35:32 |

紧接着执行 `Verify-ReferenceParity.ps1 -RequireBuildArtifacts`，全部检查通过，包括三个活动工作区工程、双核依赖链、两个 ELF、XML/FSP/引脚/UART9 等价，以及 CPU1 无 `r_ioport` 残留。

这些结果证明磁盘工程可被全新工作区构建，也证明修复后的日常活动工作区已能从 Solution GUI 入口完成双核构建；仍不等于烧录或实板验证。
