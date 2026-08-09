# DAB 当前工作空间

本目录只保留当前硬件、首版固件、最终报告和唯一物理仿真基准。历史副本与可再生临时文件已移到：

`C:\Users\LENOVO\Documents\DAB_Legacy_Archive_20260809`

## 当前唯一入口

| 内容 | 路径 | 说明 |
|---|---|---|
| 最新硬件工程 | `ProPrj_瑞萨杯_2026-08-09.epro2` | 原理图、PCB和真实网表的最高优先级事实来源 |
| RA6M5 首版固件 | `RA6M5_DAB_Test/` | 当前提交 `49acbf44706af8aeae76e57f28844d4a12479d48` |
| RA8P1 首版固件 | `C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1` | 当前提交 `8c922dc67f540ba99f2d6078c5aeb9e4c3ad2d80` |
| 唯一物理仿真 | `DAB_RA8P1_PCB_Notes/DAB_RA8P1_Final_Physical_MOSFET.slx` | 八只 AONS66917；仅作控制和工况参考，不能覆盖最新 EDA 网表 |
| 首次上电清单 | `DAB_RA8P1_PCB_Notes/04_首次上电与硬件验收清单.md` | 低功率分阶段调试入口 |
| 控制策略 | `DAB_RA8P1_PCB_Notes/RA8P1_DAB_Control_Strategy.md` | SPS/EPS/TPS、软启动与保护说明 |
| 最终报告 | `49-合肥工业大学-何润宇-刘羽凡-林学鹏-智能光储隔离功率变换器/` | 最终 DOCX、PDF及作品简介 |

## 固件状态

- RA6M5 与 RA8P1 都默认 SPS，上电不自动传输功率。
- 串口支持 `mod sps`、`mod eps`、`mod tps`、`mod auto` 和 `mod show`。
- 模式只允许在 READY、PWM和功率回路关闭时切换。
- 首次功率测试顺序：`zero`、`power 20`、`control on`。
- RA6M5 保留未提交的 Keil 用户配置 `FSP_Project.uvoptx`。
- RA8P1 保留未提交的 e² studio 用户配置 `.settings/language.settings.xml`。

## 仿真复核

MATLAB R2024b 在本目录运行：

```matlab
addpath(fullfile(pwd, 'tools'));
validate_h1p7_cold_start_corners
validate_h1p7_startup_voltage_stages
validate_h1p7_merged_physical_model
validate_h1p7_final_repeatability_and_leakage
```

仿真结果位于 `DAB_RA8P1_PCB_Notes/validation_results/`。它们是模型证据，不是实物功率、温升或效率证明。

## 基准哈希

- `ProPrj_瑞萨杯_2026-08-09.epro2`：`3CCFE86BFF829EE4CE75A6BE83E0EE56066F4AC9F99CDBB7495A23E06B2CCB69`
- `DAB_RA8P1_Final_Physical_MOSFET.slx`：`AC216A05E53BA401F579B2BEF2F419850748B5360258A5E6D021A49ECD0AA7C5`

## 清理原则

- 不在本目录保存 EasyEDA 解包、截图判断文件、DOCX 渲染缓存、依赖副本或旧固件 SREC。
- 不复制 RA8P1 工程到本目录；以 e² studio 工作区中的工程为唯一源码。
- 新的硬件判断必须以最新 `.epro2` 的原理图和 PCB 网表为准。
- Git 中的历史只用于追溯，不把旧文件重新恢复到当前工作区。
