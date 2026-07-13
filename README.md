# DAB_DCDC

基于瑞萨 RA8P1 MCU 的双有源桥 DCDC 功率变换器，面向 2026 全国大学生电子设计竞赛信息科技前沿专题赛（瑞萨杯）。

## 当前定版

- 版本：H1.7-MERGE-R1
- MATLAB/Simulink：R2024b
- 正式模型：[DAB_RA8P1_Final_Physical_MOSFET.slx](DAB_RA8P1_PCB_Notes/DAB_RA8P1_Final_Physical_MOSFET.slx)
- 模型 SHA-256：`E1A6984F58CD4A986F4B9CC2852DB166C1179535CC7A6C7694932DD02D7B0FD4`
- 功率级：T1 固定 1:2，副边折算漏感 0.50uH +/-20%，无外接传输电感，LOUT 为 SER2915H-222KL 2.2uH

正式模型以较完整物理功率级为基准，保留 MOS 封装寄生、PCB 母线寄生、TMCS 导体损耗、真实门极网络和首板无源参数，并选择性合并 TMCS 动态、快速 OC、硬件锁存、八路独立门控及 EPS 控制。没有通过理想输出源或删除损耗来替换 Vout。

## 关键结果

24V 输入、300W 负载、48V 预充、30ms 仿真，末 2ms 分析窗：

| 指标 | 结果 |
|---|---:|
| Vout 平均值 | 48.000765417V |
| 对 48V 平均误差 | +0.765417mV |
| Vout 峰峰纹波 | 4.479mV |
| 原边/副边物理峰值 | 43.141A / 21.840A |
| 重复检查 | 三次独立运行一致 |
| 0.40uH 漏感角点 | 48.001229V，5.296mVpp |
| 0.60uH 漏感角点 | 47.999922V，3.873mVpp |
| 重复 Vds 峰值 | 55.805V |
| 关断管 Miller 最大抬升 | 0.376V |
| 瞬态过流锁存 | PASS |

0.40uH 极限角点平均误差为 +1.229mV，属于已接受的轻微超差。实物达到 1mV 平均精度仍需要 ADC、分压、零偏、增益和温漂校准。

## PCB 权威文件

1. [首板硬件定版索引与结论](DAB_RA8P1_PCB_Notes/00_首板硬件定版索引与结论.md)
2. [完整原理图逐器件逐引脚接线表 PINMAP-R2](DAB_RA8P1_PCB_Notes/11_完整原理图逐器件逐引脚接线表.md)
3. [H1.7 PCB 打板前硬件冻结报告](DAB_RA8P1_PCB_Notes/14_H1.7_PCB打板前硬件冻结报告.md)
4. [双版本对比与合并验证报告](DAB_RA8P1_PCB_Notes/16_H1.7_双版本对比与合并验证报告.md)
5. [首板硬件定版 BOM](DAB_RA8P1_PCB_Notes/26_DCDC_BOM_首板硬件定版.csv)

不能直接把 Simulink 中的 ESR、DCR、LD/LS/LG、PCB 5nH、TMCS 0.7mOhm 或 Transport Delay 当作独立元件画进原理图。真实连接和采购分别以 PINMAP-R2 与 26 号 BOM 为准。

## R2024b 复现

在仓库根目录启动 MATLAB R2024b：

```matlab
apply_final_safety_controller
validate_h1p7_merged_physical_model
validate_h1p7_final_repeatability_and_leakage
validate_h1p7_hardware_freeze
validate_first_board_hardware_closure
```

`validate_h1p7_merged_physical_model` 包含额定长稳态、故障锁存和 5ns 栅极/Vds 检查；`validate_h1p7_final_repeatability_and_leakage` 包含三次独立重复及 0.40/0.60uH 角点。结果保存在 `DAB_RA8P1_PCB_Notes/final_physical_models_screenshots/`。

## 工程边界

本次新增闭环重点验证 24V/300W/48V 和漏感 +/-20%。18-28V 全负载、零伏冷启动及 500W 限定短时域仍需固件标定和实物验证。发 Gerber 前必须完成 FSP 引脚无冲突、逐网 ERC、换流环路、隔离、热机械和封装复核；焊接后必须完成限流上电、双脉冲、故障注入和热像测试。
