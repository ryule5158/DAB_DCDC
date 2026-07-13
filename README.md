# DAB_DCDC

基于瑞萨 RA8P1 的双有源桥 DCDC 功率变换器。仓库仅保留当前 H1.7 定版交付物，所有文件集中在 [`release/`](release/) 中。

## 最终交付物

| 文件 | 用途 |
|---|---|
| [`DAB_RA8P1_Final_Physical_MOSFET.slx`](release/DAB_RA8P1_Final_Physical_MOSFET.slx) | MATLAB/Simulink R2024b 最终物理 MOSFET 模型 |
| [`DAB_RA8P1_首板硬件定版BOM.csv`](release/DAB_RA8P1_首板硬件定版BOM.csv) | 179 行机器可读 BOM |
| [`DAB_RA8P1_首板硬件定版BOM_H1.7.xlsx`](release/DAB_RA8P1_首板硬件定版BOM_H1.7.xlsx) | 适合查看与采购核对的 Excel BOM |
| [`DAB_RA8P1_完整原理图逐引脚接线表.md`](release/DAB_RA8P1_完整原理图逐引脚接线表.md) | 画原理图时使用的逐器件、逐引脚连接依据 |
| [`DAB_RA8P1_四层PCB布局布线规范.md`](release/DAB_RA8P1_四层PCB布局布线规范.md) | 四层 PCB、功率回路、驱动与隔离布局要求 |
| [`DAB_RA8P1_H1.7_硬件冻结报告.md`](release/DAB_RA8P1_H1.7_硬件冻结报告.md) | 首板硬件边界、保护与下单前检查结论 |
| [`DAB_RA8P1_H1.7_双版本对比与验证报告.md`](release/DAB_RA8P1_H1.7_双版本对比与验证报告.md) | 两套模型对比、合并过程和扩展工况验证结果 |

## 冻结状态

- MATLAB 版本：R2024b。
- 模型 SHA-256：`E1A6984F58CD4A986F4B9CC2852DB166C1179535CC7A6C7694932DD02D7B0FD4`。
- H1.7 硬件冻结审计：`86/86 PASS`。
- BOM：179 行，无空引用位号、无重复引用位号。
- 最终变压器：固定 `1:2`，副边实测漏感 `0.50uH +/-20%`，板上不增加外接传输电感。

## 使用顺序

1. 先读硬件冻结报告，确认不能改动的参数和首板边界。
2. 按逐引脚接线表绘制原理图，并以 CSV/Excel BOM 放置元件。
3. 按四层 PCB 规范完成布局布线，重点复核换流环路、Kelvin 回路、隔离禁铜和保护链。
4. 用最终模型复核控制策略；实板仍需按报告要求完成 ERC、封装、热设计和分级上电验证。

历史脚本、重复副本、缓存、聊天记录和中间验证文件不再上传到此仓库，避免与最终定版资料混用。
