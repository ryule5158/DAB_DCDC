# 两模型首轮训练记录（2026-08-21）

## 结论

两条训练、全 INT8 导出和 PC 端 TFLite 回读均已完成，但两个模型均为
`deployment_ready=false`。本轮只证明现有数据 ETL、分组切分、训练和量化链路可运行；
禁止提交 RUHMI 正式编译、写入固件或用于实时控制。

| 模型 | 实测数据输入 | INT8 结果 | 部署门禁 |
|---|---:|---:|---|
| SPS/EPS/TPS 调制选择及参数包 | 4 个独立工况；TPS 2、EPS 2、SPS 0 | mode accuracy 0.000；bundle accuracy 0.000 | 未通过 |
| MPPT Vmpp 候选 | 10 条独立 P&O 轨迹；全部真值未验证 | exact-bin accuracy 0.000；MAE 2.500 V | 未通过 |

## 数据边界

- 调制标签来自两条明确 `selected_overall` 记录，以及两组完成 SPS/EPS/TPS
  三模式匹配比较后按板端相对 `Pout/Pin` 选择的 provisional 赢家。
- 调制记录没有独立输入功率/效率真值，参数包也没有达到每包三次重复。
- MPPT 输入由既有 `ALG:` P&O 轨迹按电压聚合并插值形成，不是完整、独立的
  P-V 扫描；全部保持 `ground_truth_validated=false`。
- MPPT provisional `true_vmpp_v` 由同一九点输入功率的最大值派生，存在直接的
  标签-特征依赖；本轮测试指标不能代表对独立曲线的泛化能力。
- 本轮未访问 COM4、COM24、电子负载接口或测试固件。

## 产物

- `modulation_selector_v1_run1/modulation_selector_int8.tflite`
  - shape: INT8 `[1,15] -> [1,7]`
  - SHA-256: `e5d3ab73e87d075dcf4cc7c5a399f0c4f5c05d8cd68975d59d0af91d5cc3694f`
- `mppt_v1_run1/mppt_vmpp_int8.tflite`
  - shape: INT8 `[1,9] -> [1,13]`
  - SHA-256: `365852c98f39915e936c166721ce6b9f7356d100e990753be86ed77574e5f59d`

## 正式训练仍需

- 调制模型：至少 60 个独立匹配工况、180 个重复标签，SPS/EPS/TPS 各至少
  10 行，每个参数包至少 3 次一致重复，并补齐独立效率标签。
- MPPT 模型：至少 50 个 episode、10 条独立曲线、至少 4 个 Vmpp 类别；每条
  `true_vmpp_v` 必须来自完整可复现 P-V 扫描或其他独立真值。

自动质量审计结果保存在 `DATA_QUALITY_AUDIT.json`；该审计要求两模型持续保持
`deployment_ready=false`、训练/验证/测试分组无交叉、TFLite 输入输出全 INT8，
且文件 SHA-256 与元数据一致。
