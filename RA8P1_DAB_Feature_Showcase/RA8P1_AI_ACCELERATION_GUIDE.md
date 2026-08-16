# RA8P1 M85 / Helium / Ethos-U55 实现与边界

## 实现状态

- 固件：`2026-07-30-production-ai-v2`。
- Cortex-M85 确定性路径：ADC-DMA 每三个 299.760 ksample/s 一致帧更新一次，控制频率约 99.92 kHz。
- Helium：一次向量化转换 ADC0/ADC1 合并帧的前八个通道，第九通道保持边界安全的标量转换。
- TCM：ADC 服务、保护判定、PI/SPS 控制和 PWM 相移更新位于 ITCM；控制状态位于 DTCM；DMA 环形帧位于非缓存 SRAM。
- Ethos-U55：FSP `rm_ethosu` + Arm Ethos-U Core Driver 25.2.0，使用异步 `ethosu_invoke_async()` 与非阻塞 `ethosu_wait(false)`。
- RUHMI INT8 模型：MPPT（9→13）、PV 故障（32×3→3，含 CPU 反量化分区）、热状态（8→3）。生成符号均带模型前缀，三模型可共存。

## 安全合同

AI 上电默认关闭，只有 `ai on` 才允许排队推理。AI 输出只进入 `dab_ai_supervisor_power_cap()`：返回值永远不高于操作者通过 `power N` 给出的上限；PV 异常或热降额只能把上限降到 20 W，热预警只能降到 75%。AI 无权直接写 GPT、RUN_REQ、预充或保护 GPIO。

硬件比较器、POEG、ADC 新鲜度、电压/电流/功率/温度保护、USB 断线关断和确定性 PI/SPS 状态机始终独立运行。低置信度或 NPU 错误不会绕过这些保护；故障锁存会立即关闭 AI 并执行原有安全关断。

## 关键文件

| 文件 | 责任 |
|---|---|
| `src/dab_measurements.c` | Helium ADC 工程量转换与滤波 |
| `src/dab_accel.h` | ITCM、DTCM、非缓存 DMA 段属性 |
| `src/dab_production.c` | PI/SPS、相移斜坡、软限流和故障判定 |
| `src/dab_ai_supervisor.c` | 三模型调度、结果有效性和只降功率策略 |
| `src/ai/ruhmi_bridge.c` | 单 NPU 作业异步桥及 PV CPU 后处理 |
| `src/ai/*_model.c` | 物理量归一化、INT8 量化、输出解码与 0.70 置信度门限 |
| `tools/import_ruhmi_models.py` | 可重复导入 RUHMI 生成代码并进行符号前缀隔离 |

## 命令与验证边界

```text
ai status
ai on
ai mppt 120,135,151,166,174,170,158,141,122
ai off
```

2026-07-30 已用 e² studio/FSP 6.4.0 + GCC Arm 13.2.1 完整生成并链接 Debug ELF。构建验证证明 API、模型数据、NPU 驱动和内存布局一致；尚未代替开发板上的 NPU 推理时延/分类正确性实测，也未代替 20–200 W 功率级验证。首次启用仍应先保持 `control off`，单独执行 `ai on` 和 `ai status`，确认 NPU 无错误后再从 20 W 分级测试。
