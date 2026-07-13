# RA8P1 控制 DAB DC-DC 的控制方式与控制思路

本文档面向当前最终 MOSFET DAB 仿真模型：

- 模型文件：`DAB_RA8P1_Final_Physical_MOSFET.slx`
- 硬件接入输入：`18-32V`；普通 SPS 额定运行：`18-28V`
- 输出：`36V-48V`，300W 连续还必须满足 `|Vout-2Vin|<=8V`
- 常规验证功率：`20W/50W/100W`
- 高功率短时验证：`300W/500W`
- 最终输出滤波：`LOUT=SER2915H-222KL, 2.2uH/2.05mOhm最大值`；桥前 `CSEC=94uF` 并带独立 `CSEC_HF=40uF有效值`；桥后 `COUT=8000uF/3.875mOhm` 并带另一组 `COUT_HF=40uF有效值`。两组 HF 电容物理上均为 8只 10uF/100V 软端头 MLCC，不能合并。
- 当前功率 MOSFET：`AONS66917T`（模型内部简称 `AONS66917`）
- 输出端两只旧的独立“EMI 电感”已经删除；它们不是耦合共模扼流圈，会额外引入差模电感和谐振

## 1. 控制方式总览

本节的单移相控制只用于 `Vout>=36V` 后的 NORMAL_SPS 稳态。primary bridge 和 secondary bridge 都输出 50% 占空比方波，通过两桥之间的相位差调节功率传输。`Vout<36V` 的真实冷启动必须使用受峰值电流约束的 EPS/PDM/三电平窄脉冲，不能把普通 SPS 的相移缓升当作冷启动方案。

控制核心不是调 PWM 占空比，而是调 secondary bridge 相对 primary bridge 的相移量：

- 相移增大：传输功率增加或方向改变，具体符号由模型定义。
- 相移减小：传输功率降低。
- 所有桥臂保持互补驱动，并加入死区，避免上下管直通。

最终模型采用的基本时序：

- 开关频率：`300 kHz`
- PWM 周期：`3.333 us`
- 死区时间：`120 ns`
- 控制周期：`10 us`，约等于每 3 个开关周期更新一次控制量
- 相移限幅：`-0.55 us` 到 `+0.46 us`

## 2. 单片机外设分工

RA8P1 推荐外设分工如下：

- GPT 定时器：生成 8 路互补 PWM，驱动 DAB 的 4 个半桥。
- POEG：硬件故障关断通道，接过流/过压比较器输出。
- ADC：采样 `Vin`、`Vout`、`Iout`，必要时增加 primary current。
- DTC/DMAC：把 ADC 结果搬运到缓冲区，降低 CPU 抖动。
- AGT/GPT 中断：每 `10 us` 执行一次控制计算。

硬件保护必须独立于软件闭环：

- 快速过流比较器直接进 POEG 或驱动器使能关断。
- 软件 ADC 只负责调节和慢速保护，不承担最短路保护。

## 3. 采样量与滤波

控制器需要以下反馈：

- `Vin`：硬件测量覆盖 `18-32V`；NORMAL_SPS 只允许 `18-28V`。
- `Vout`：输出电压，目标限制在 `36-48V`。
- `Iout`：输出电流绝对值，用于负载估算和前馈补偿。

最终仿真中的数字滤波逻辑：

```c
vFilt   = vFilt   + 0.40f * (Vout - vFilt);
vinFilt = vinFilt + 0.10f * (clamp(Vin, 18.0f, 28.0f) - vinFilt);
iFilt   = iFilt   + 0.22f * (fabsf(Iout) - iFilt);
```

其中 `vFilt` 要快一些，因为这次长时波形偏低的问题本质上是电压环纠偏太慢。

## 4. 相移控制结构

每个 `10 us` 控制周期执行一次：

1. 限制输出设定值：`Vref = clamp(VrefCmd, 36V, 48V)`。
2. 滤波 `Vin/Vout/Iout`。
3. 连续 20 个控制周期确认 `18<=Vin<=28V、36<=Vout<=49V、|Vout-2Vin|<=7.5V`，才锁存 NORMAL_SPS 许可。
4. 运行中若失配达到 8V、Vin 超出保持边界或硬件故障出现，清除许可并把八路栅极命令置零。
5. 计算电压误差：`err = Vref - vFilt`。
6. 通过前馈函数 `base_phase(Vref, vinFilt, iFilt)` 得到基础相移。
7. 用 trim 环节修正模型误差和负载变化，限幅后更新 GPT 缓冲寄存器。

最终仿真对应的 trim 更新式：

```c
trim = 0.965f * trim
     + 0.0102e-6f * err
     + 0.0033e-6f * (err - prevErr);

trim = clamp(trim, -0.100e-6f, +0.100e-6f);
```

轻载时可以收窄 trim 限幅，避免空载或极轻载下抖动：

```c
if (iFilt < 0.70f) {
    trimLimit = 0.016e-6f;
} else {
    trimLimit = 0.100e-6f;
}
```

## 5. 前馈相移思路

前馈相移用于让闭环一开始就接近正确工作点，避免 PI 全靠积分慢慢拉。

核心判断量：

```c
buckDelta = Vin - 0.5f * Vref;
```

当 `buckDelta > 0.6` 时，模型处在一类 buck-like 区域；否则接近 boost-like 区域。当前 24V 输入、48V 输出时：

```text
buckDelta = 24 - 0.5 * 48 = 0
```

所以 48V 输出点主要落在 boost-like 区域。

最终模型在 48V/100W 附近保留很小的长时校准：

```c
phaseUs += 0.020f
         * expf(-powf(buckDelta / 0.75f, 2.0f))
         * expf(-powf((iFilt - 2.1f) / 1.0f, 2.0f));
```

这项只在 `Vin≈24V, Vout≈48V, Iout≈2.1A` 附近明显生效，用来补偿 48V/100W 附近的慢速偏差。变压器匝比被视为硬件约束，不能为了仿真均值再改变。

最终保存模型中的高频变压器参数为：

```text
L1 = 57.5uH
L2 = 230uH
k  = 0.999999
Ns/Np = sqrt(230 / 57.5) = 2.0000
```

这保持原 `1:2` 变压器，不通过副边匝比来抬高输出。当前验收条件是 6–10ms 后半段平均误差 `<1mV` 且纹波峰峰值 `<0.1Vpp`。

高负载 boost 校准使用较小值：

```c
phaseUs += 0.010f * (1.0f - expf(-max(iFilt - 7.0f, 0.0f) / 2.0f));
```

这个值不能太大，否则 48V/500W 短时工况会被抬高。

## 6. PWM 生成方式

四个半桥可按下列逻辑生成：

```c
primary_A  = square(t, 0);
primary_B  = square(t + Tsw / 2, 0);
secondary_A = square(t - phase, 0);
secondary_B = square(t - phase + Tsw / 2, 0);
```

每个 half bridge 内部必须输出互补 PWM：

- high-side 在前半周期导通。
- low-side 在后半周期导通。
- 两者之间加入 `120ns` 死区。

实际 RA8P1 中不建议在中断里手工翻 GPIO，应该使用 GPT complementary PWM 和 buffer compare update。

## 7. 软件流程

主循环只做慢任务：

```c
while (1) {
    handle_comm();
    update_display_or_telemetry();
    check_noncritical_faults();
}
```

控制中断每 `10us` 执行：

```c
void control_isr_10us(void)
{
    read_adc_filtered_values();

    Vref = clamp(VrefCmd, 36.0f, 48.0f);
    update_vin_vout_iout_filters();

    err = Vref - vFilt;
    dErr = err - prevErr;

    phaseBase = base_phase(Vref, vinFilt, iFilt);
    trim = update_trim(err, dErr, iFilt);

    phaseUs = phaseBase + trim * 1e6f;
    phaseUs += long_trace_48v_calibration(Vref, vinFilt, iFilt);
    phaseUs += high_load_calibration(iFilt);

    phase = clamp(phaseUs * 1e-6f, -0.55e-6f, 0.46e-6f);
    update_gpt_phase_registers(phase);

    prevErr = err;
}
```

## 8. 启动与软启动

启动必须分成两个不同调制状态：

1. 上电后 `RUN_REQ=0`，检查 Vin、驱动电源、隔离电源、传感器零点和全部故障脚。
2. `Vout<36V` 时进入 STARTUP_CURRENT_LIMITED；普通 50% 方波 SPS 仍禁止。
3. 使用 EPS/PDM/三电平窄脉冲逐包充电，每包之后检查高频电流；建议 `Ipri_pk<=35A、Isec_pk<=20A`。
4. 当实测 Vout 已进入 36-48V，且连续 20 个控制周期满足 `|Vout-2Vin|<=7.5V`，才进入 NORMAL_SPS。
5. NORMAL_SPS 中保持边界为 `<8V`；越界先停止增加功率，再清除 RUN 许可。

当前保存的物理模型仍用 48V 电容初值进行稳态回归，因此只证明 NORMAL_SPS 许可和稳态控制。它没有证明 0V 冷启动；冷启动状态机完成前，不能删除这一条限制说明。

## 9. 保护策略

硬件快速保护：

- MOSFET 过流：比较器直接关断驱动或触发 POEG。
- 输出短路：比较器快速锁存，软件只负责记录和复位。
- 驱动欠压：驱动器自身 UVLO 与 RA8P1 fault 输入共同处理。

软件慢速保护：

- `Vin < 18V` 或 `Vin > 28V`：禁止 NORMAL_SPS；`28-32V` 仅允许测量和保护链上电。
- `Vin > 32V`：输入越界停机；硬件 OVP 仍约 34.1V。
- `Vout` 硬件过压理论跳闸约 52.01V、释放约 51.49V；软件阈值应更低并留 ADC/电阻误差余量。
- `|Vout-2Vin| >= 8V`：NORMAL_SPS 退出并降功率/停机。
- `Iout` 超过连续电流限制：降额或停机。
- 温度过高：降额或停机。

故障恢复建议人工或上位机确认，不建议短路后自动反复重启。

## 10. 标称点历史验证与当前边界

H1.1 运行域许可回归：`24V->48V` 时最大相移约 `0.261us`，正常开桥；`30V->48V` 和 `32V->36V` 从输入上升全过程的最大相移均为 `0us`，没有经过合法区时的瞬时误开桥。该回归验证的是稳态 SPS 许可，不是冷启动。

变压器 `1:2` 匝比保持不变，最终保存模型按后半段平均值和纹波同时验收：

- 工况：`Vin=24V, Vref=48V, Pout=100W`
- 仿真时间：`10ms`
- 统计窗口：`6-10ms`
- 三遍验证一致：
  - 平均输出：`47.999281600V`
  - 平均误差：`-0.718mV`
  - 纹波：`0.070961Vpp`
  - 后半段输出电流：`2.059A` 到 `2.142A`

这组数据是固定 `1:2` 变压器下的标称点历史验证，证明功率级和滤波器具备达到该指标的能力；它不代表 18–28V 二维工作域已经完成控制标定。当前 H1.7 硬件正反例、冷启动、联锁和 PCB 准入结论统一见 `14_H1.7_PCB打板前硬件冻结报告.md`，旧三次验证报告和旧波形文件已经清理。

## 11. PCB 前仍需确认

仿真与首板 BOM 中的最终输出电感已经锁定为 Coilcraft `SER2915H-222KL`，旧的 `4.7uH` 和 `MTP2920-2R2MC` 均不得再下单：

- `2.2uH`
- 40°C 温升参考电流 `30A`
- 10% 降感电流 `100A`；20%/30% 降感电流均大于 `100A`
- DCR 最大 `2.05mOhm`
- 最终 PCB 中必装且不设铜排、0Ω或焊锡旁路
- 300kHz 实际纹波下仍必须做样机温升验证

输出电容组已经落实为 `8 x UR080M102J35TC97S00A`，合计 `8000uF/80V`、等效 ESR `3.875mOhm`、纹波额定合计 `27.2A@100kHz`。PCB 仍需确认 8 只 `D16x35mm` 电容的体积、等长并联铜皮和浪涌电流。
