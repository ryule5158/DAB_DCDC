# DAB_DCDC

基于瑞萨 RA8P1 的双有源桥 DCDC 功率变换器统一工程。仓库保存硬件交付、仿真、双核固件、串口实测数据和训练数据；实物结论以原生 EDA 网表与仪器记录为准。

## 当前入口

| 内容 | 路径 | 说明 |
|---|---|---|
| 双核固件 | [`RA8P1_DAB_Feature_Showcase/dual_core/`](RA8P1_DAB_Feature_Showcase/dual_core/) | CPU0 实时控制，CPU1 FreeRTOS 能量管理 |
| v39 烧录文件 | [`firmware_v39/`](DAB_RA8P1_PCB_Notes/live_tuning/2026-08-15_24V_resistive_multimode/firmware_v39/) | CPU0、CPU1 S-record 与 manifest |
| 电子负载清单 | [`V39_ELECTRONIC_LOAD_CHECKLIST.md`](DAB_RA8P1_PCB_Notes/live_tuning/2026-08-15_24V_resistive_multimode/V39_ELECTRONIC_LOAD_CHECKLIST.md) | 新功能串口调用与分级测试顺序 |
| 工作记录 | [`WORKLOG.md`](DAB_RA8P1_PCB_Notes/live_tuning/2026-08-15_24V_resistive_multimode/WORKLOG.md) | 固件、数据、参数和验证边界 |
| 训练字段 | [`TRAINING_DATA_SCHEMA.md`](DAB_RA8P1_PCB_Notes/live_tuning/2026-08-15_24V_resistive_multimode/TRAINING_DATA_SCHEMA.md) | SPS/EPS/TPS 同工况训练格式 |
| 最新硬件工程 | `ProPrj_瑞萨杯_2026-08-09.epro2` | 原理图、PCB和真实网表的最高优先级事实来源 |
| H1.7 冻结交付 | [`release/`](release/) | 合并保留的远端硬件冻结历史 |

## v39 兼容边界

- 上电仍为昨天验证过的单电压环，能量管理、MPPT、充电控制与 AI 提示均默认关闭。
- 原有 `DAB:` 串口字段顺序不变；扩展 `ALG:` 日志默认关闭。
- 新功能只在串口显式执行 `arch current`、`arch energy` 或相关能量配置命令后参与控制。
- `ai mod sps|eps|tps` 只记录候选调制方式，不会自动切换功率调制。
- `save` 在未启用新功能时继续生成旧格式记录；`alglog off` 可恢复为仅输出旧记录。

## H1.7 冻结交付物

### 最终交付物

| 文件 | 用途 |
|---|---|
| [`DAB_RA8P1_Final_Physical_MOSFET.slx`](release/DAB_RA8P1_Final_Physical_MOSFET.slx) | MATLAB/Simulink R2024b 最终物理 MOSFET 模型 |
| [`DAB_RA8P1_首板硬件定版BOM.csv`](release/DAB_RA8P1_首板硬件定版BOM.csv) | 179 行机器可读 BOM |
| [`DAB_RA8P1_首板硬件定版BOM_H1.7.xlsx`](release/DAB_RA8P1_首板硬件定版BOM_H1.7.xlsx) | 适合查看与采购核对的 Excel BOM |
| [`DAB_RA8P1_完整原理图逐引脚接线表.md`](release/DAB_RA8P1_完整原理图逐引脚接线表.md) | 画原理图时使用的逐器件、逐引脚连接依据 |
| [`DAB_RA8P1_四层PCB布局布线规范.md`](release/DAB_RA8P1_四层PCB布局布线规范.md) | 四层 PCB、功率回路、驱动与隔离布局要求 |
| [`DAB_RA8P1_H1.7_硬件冻结报告.md`](release/DAB_RA8P1_H1.7_硬件冻结报告.md) | 首板硬件边界、保护与下单前检查结论 |
| [`DAB_RA8P1_H1.7_双版本对比与验证报告.md`](release/DAB_RA8P1_H1.7_双版本对比与验证报告.md) | 两套模型对比、合并过程和扩展工况验证结果 |

### 冻结状态

- MATLAB 版本：R2024b。
- 模型 SHA-256：`580E5A5A10FD5BF3788AD1598480B99ABF9739F4D72B83A485ABA804762194DF`。
- H1.7 硬件冻结审计：`86/86 PASS`。
- BOM：179 行，无空引用位号、无重复引用位号。
- 最终变压器：固定 `1:2`，副边实测漏感 `0.50uH +/-20%`，板上不增加外接传输电感。

### 使用顺序

1. 先读硬件冻结报告，确认不能改动的参数和首板边界。
2. 按逐引脚接线表绘制原理图，并以 CSV/Excel BOM 放置元件。
3. 按四层 PCB 规范完成布局布线，重点复核换流环路、Kelvin 回路、隔离禁铜和保护链。
4. 用最终模型复核控制策略；实板仍需按报告要求完成 ERC、封装、热设计和分级上电验证。

远端原有 `release/` 历史与当前研发工程合并保留，禁止用强制推送覆盖任一侧历史。仿真和静态检查不等同于实物功率、温升或效率验证。
