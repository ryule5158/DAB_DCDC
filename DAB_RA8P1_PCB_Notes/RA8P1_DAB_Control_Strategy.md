# RA8P1 控制 DAB DC-DC 的控制方式与控制思路

本文档面向当前最终 MOSFET DAB 仿真模型：

- 模型文件：`DAB_RA8P1_Final_Physical_MOSFET.slx`
- 输入：`24V` 标称
- 输出：`36V-48V`
- 常规验证功率：`20W/50W/100W`
- 高功率短时验证：`300W/500W`
- 最终输出滤波：`LOUT=2.2uH/2.5mOhm`，`COUT=4700uF/10mOhm`，`COUT_HF=110uF/0.8mOhm`

## 1. 控制方式总览

本电路采用 DAB 双有源桥的单移相控制方式，也就是 primary bridge 和 secondary bridge 都输出 50% 占空比方波，通过两桥之间的相位差调节功率传输。

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

- `Vin`：输入电压，范围按 `18-32V` 处理。
- `Vout`：输出电压，目标限制在 `36-48V`。
- `Iout`：输出电流绝对值，用于负载估算和前馈补偿。

最终仿真中的数字滤波逻辑：

```c
vFilt   = vFilt   + 0.40f * (Vout - vFilt);
vinFilt = vinFilt + 0.10f * (clamp(Vin, 18.0f, 32.0f) - vinFilt);
iFilt   = iFilt   + 0.22f * (fabsf(Iout) - iFilt);
```

其中 `vFilt` 要快一些，因为这次长时波形偏低的问题本质上是电压环纠偏太慢。

## 4. 相移控制结构

每个 `10 us` 控制周期执行一次：

1. 限制输出设定值：`Vref = clamp(VrefCmd, 36V, 48V)`。
2. 滤波 `Vin/Vout/Iout`。
3. 计算电压误差：`err = Vref - vFilt`。
4. 通过前馈函数 `base_phase(Vref, vinFilt, iFilt)` 得到基础相移。
5. 用 trim 环节修正模型误差和负载变化。
6. 加入 48V/100W 附近的长时精确校准项。
7. 限幅相移并更新 GPT 比较寄存器。

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

这项只在 `Vin≈24V, Vout≈48V, Iout≈2.1A` 附近明显生效，用来补偿 48V/100W 附近的慢速偏差；最终 1mV 精度由电压反馈分压校准实现，而不是继续加大相移。

输出电压反馈进入 MCU ADC 前加入一个硬件等效校准：

```c
vout_adc = 0.999530f * vout_sensed;
```

在实际电路中，这对应输出电压分压电阻/隔离放大器/ADC 标定系数的微调。它使 MCU 看到的电压略低于真实输出，闭环会把真实输出抬到 48V 附近。该校准量约为 `0.047%`，可以由 0.1% 精密分压电阻搭配软件标定或校准电阻实现。

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

建议启动顺序：

1. 上电后保持 PWM 禁止。
2. 检查 `Vin` 是否在允许范围。
3. 检查驱动电源、隔离电源和故障脚状态。
4. 预置 `vFilt = Vout`，避免初始误差过大。
5. 将 `VrefCmd` 从当前输出电压缓慢爬升到目标值。
6. 逐步释放相移限幅，例如先限制到 `±0.08us`，再放宽到最终范围。
7. 输出稳定后进入正常闭环。

不要一上电就直接给最大相移，否则输出电容组较大时容易出现长时间低频摆动。

## 9. 保护策略

硬件快速保护：

- MOSFET 过流：比较器直接关断驱动或触发 POEG。
- 输出短路：比较器快速锁存，软件只负责记录和复位。
- 驱动欠压：驱动器自身 UVLO 与 RA8P1 fault 输入共同处理。

软件慢速保护：

- `Vin < 18V` 或 `Vin > 32V`：停机。
- `Vout > 52V`：停机。
- `Iout` 超过连续电流限制：降额或停机。
- 温度过高：降额或停机。

故障恢复建议人工或上位机确认，不建议短路后自动反复重启。

## 10. 当前最终仿真验证结果

针对用户指出的 9ms 后输出没有到 48V 的问题，最终保存模型的验证口径为：

- 工况：`Vin=24V, Vref=48V, Pout=100W`
- 仿真时间：`10ms`
- 统计窗口：`5-10ms`
- 三遍验证一致：
  - 平均输出：`47.9999957V`
  - 平均误差：`-0.0000043V`
  - 最终点：`48.00432V`
  - 纹波：`0.09381Vpp`

这说明输出已经围绕 48V 工作，不再是上一版 47.8V-47.95V 那种整体偏低状态。

## 11. PCB 前仍需确认

仿真中最终输出电感已经改为 `2.2uH/2.5mOhm`，因此旧的 `4.7uH` 电感不能再直接下单作为最终方案。PCB 前必须重新选择：

- `2.2uH`
- 连续电流 `>=30A`
- 饱和电流 `>=40A`
- DCR 尽量接近或低于 `2.5mOhm`
- 温升和磁芯损耗满足 300kHz 工作

输出 `4700uF` 电容组也需要落实为真实可采购的 80V/100V 低 ESR 电容组合，并核算纹波电流和浪涌电流。
