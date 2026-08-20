# RA8P1 DAB 两模型训练工具

本目录只训练项目确定的两个模型：

1. `train_modulation_selector.py`：同一个小型网络同时输出 SPS/EPS/TPS 分数和已验证 `parameter_bundle_id` 分数。
2. `train_mppt.py`：根据 18--24 V 的九点功率扫描输出 0.5 V 分辨率的 Vmpp 候选。

模型只输出建议。固件仍负责参数合法性、置信度/TTL、驻留时间、无扰切换、失败回退、功率和电流边界；模型不能写 PWM。

## 现有环境

使用已经验证的训练解释器：

```powershell
$python = 'C:\Renesas\AI\model-runtime\.venv\Scripts\python.exe'
Set-Location 'C:\Users\LENOVO\Desktop\Electric Simulation\RA8P1_DAB_Feature_Showcase\dual_core\tools\ai_training'
& $python --version
```

官方 RA8P1 编译环境：

```powershell
& 'C:\Renesas\AI\Verify-RenesasAI.ps1'
```

当前原生 Windows TensorFlow 使用 CPU。模型很小，第一版不需要 GPU。

## 1. 整理调制选择标签

旧数据是一行一个调参 trial，不能直接训练模式分类。转换器只提升明确标记为 `selected_overall=true` 的最终赢家，不会把 `selected_within_mode` 或失败 trial 偷换成正标签：

```powershell
& $python .\prepare_modulation_dataset.py `
  --input `
    '..\..\..\..\DAB_RA8P1_PCB_Notes\live_tuning\2026-08-16_RK8510_v39\modulation_training_24v_cv36_i6.csv' `
    '..\..\..\..\DAB_RA8P1_PCB_Notes\live_tuning\2026-08-16_RK8510_v39\modulation_training_24v_cv36_i7_v42.csv' `
  --output .\data\modulation_conditions.csv
```

正式数据使用 `schemas/modulation_condition_schema.csv`。一行代表一个已经完成三模式公平调参的工况/重复，不是一帧 UART。相移、inner、PI 和 slew 保存进参数包，模型只选择包 ID。

默认正式门槛：

- 至少 60 个独立匹配工况和 180 个重复标签；
- SPS/EPS/TPS 三类齐全，每类至少 10 行；
- 每个参数包至少 3 次重复且参数内容一致；
- `fair_comparison_eligible=true`；
- 默认只接受 `external_efficiency_validated=true`；
- 按完整 `condition_id` 分割训练/验证/测试，禁止把同一工况的相邻数据泄漏到不同集合。

正式训练：

```powershell
& $python .\train_modulation_selector.py `
  --data .\data\modulation_conditions.csv `
  --output .\artifacts\modulation_selector_v1
```

数据管线冒烟可加 `--allow-smoke --allow-provisional-labels`。这种输出的 `metadata.json` 会保持 `deployment_ready=false`，不能作为最终固件模型。

## 2. MPPT episode 数据

正式数据使用 `schemas/mppt_episode_schema.csv`。`p0_w` 到 `p8_w` 固定对应：

```text
18.00, 18.75, 19.50, 20.25, 21.00, 21.75, 22.50, 23.25, 24.00 V
```

`true_vmpp_v` 必须来自完整扫描、可复现曲线真值或其他独立参考，不能把 P&O 最后停留点直接当真值。训练/验证/测试按完整 `curve_id` 分组。

默认正式门槛：至少 50 个 episode、10 条独立曲线、至少 4 个 Vmpp 电压类别，并且全部 `ground_truth_validated=true`。

```powershell
& $python .\train_mppt.py `
  --data .\data\mppt_episodes.csv `
  --output .\artifacts\mppt_v1
```

该 v1 模型输出 18--24 V、0.5 V 间隔的 13 个类别。部署时固件适配器必须使用 `metadata.json` 中的 `vmpp_min_mv=18000`、`vmpp_step_mv=500`；不能继续沿用旧模型的 19 V/1 V 解码常量。

## 3. 输出与 RUHMI 编译

每次训练输出：

- 完全 INT8 的 `.tflite`；
- TFLite 输入/输出量化比例和 SHA-256；
- 固定特征顺序、物理范围和标签表；
- float/INT8 测试指标；
- `deployment_ready` 和全部不合格原因；
- 网络结构 JSON、float 权重和训练历史；
- 调制模型额外输出 `parameter_bundles.json`。

候选 TFLite 通过数据与指标门槛后再交给 RUHMI：

```powershell
& 'C:\Renesas\AI\Compile-RA8P1Models.ps1' `
  -ModelPath .\artifacts\modulation_selector_v1\modulation_selector_int8.tflite `
  -OutputDirectory 'C:\Renesas\AI\runs\dab_modulation_selector_v1'

& 'C:\Renesas\AI\Compile-RA8P1Models.ps1' `
  -ModelPath .\artifacts\mppt_v1\mppt_vmpp_int8.tflite `
  -OutputDirectory 'C:\Renesas\AI\runs\dab_mppt_v1'
```

RUHMI/Vela 编译通过只证明算子、量化和 C 代码生成兼容。还必须独立完成固件集成、板上推理时延、输出解码、AI OFF 回退和真实控制效果验证。
