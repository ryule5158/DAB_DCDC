# DAB H1.7 定版工程

本目录只保留 PCB 设计、R2024b 仿真复现和首板验证需要的最终文件。不要从 Git 历史或其他旧目录恢复 `_1`、H1.6、候选扫参或缓存文件。

## 直接使用

| 目的 | 唯一入口 |
|---|---|
| 打开最终仿真 | `DAB_RA8P1_PCB_Notes/DAB_RA8P1_Final_Physical_MOSFET.slx` |
| 画完整原理图 | `DAB_RA8P1_PCB_Notes/11_完整原理图逐器件逐引脚接线表.md` |
| 采购与放置元件 | `DAB_RA8P1_PCB_Notes/26_DCDC_BOM_首板硬件定版.csv` |
| 判断能否发 Gerber | `DAB_RA8P1_PCB_Notes/14_H1.7_PCB打板前硬件冻结报告.md` |
| 查看最终仿真结论 | `DAB_RA8P1_PCB_Notes/16_H1.7_双版本对比与合并验证报告.md` |
| 从零理解电路 | `DAB_RA8P1_PCB_Notes/DAB_RA8P1_从零理解仿真与MCU控制.md` |

正式模型 SHA-256：`580E5A5A10FD5BF3788AD1598480B99ABF9739F4D72B83A485ABA804762194DF`。

## 目录说明

- `DAB_RA8P1_PCB_Notes/`：唯一正式模型、编号硬件文档、BOM 和最终器件资料。
- `DAB_RA8P1_PCB_Notes/validation_results/`：只保存当前 H1.7 硬件/控制结论仍引用的最终 CSV 证据。
- `tools/`：模型安装、结构合并和最终验证脚本。所有脚本按 MATLAB R2024b 验证。
- `outputs/h1p7_bom/`：由 26 号 CSV 生成的筛选版 H1.7 Excel BOM 及生成器。
- `对话与工作记录.md`：本地工作记录，不上传 GitHub 发布目录。

## R2024b 验证

在本目录启动 MATLAB R2024b 后执行：

```matlab
addpath(fullfile(pwd, 'tools'));
validate_h1p7_hardware_freeze
validate_first_board_hardware_closure
validate_h1p7_merged_physical_model
validate_h1p7_final_repeatability_and_leakage
```

额定验证结果为 24V 输入、300W 负载时 `48.000765V`、`4.479mVpp`；三次独立复跑一致。0.40uH 漏感角点为 `48.001229V`、`5.296mVpp`，属于已接受的轻微平均误差超差。

## 清理规则

以下内容不属于定版工程：`*_1.*`、`slprj/`、`*.slxc`、H1.6 覆盖文件、候选器件扫参、旧波形图片、旧 BOM、旧模型和嵌套仓库。MATLAB 重新生成的缓存由 `.gitignore` 排除，可随时删除。
