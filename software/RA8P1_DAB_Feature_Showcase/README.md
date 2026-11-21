# RA8P1 DAB Production

RA8P1 双有源桥正式业务固件候选版。工程由已完成 PWM、ADC-DMA、USB CDC 和 GPIO 联调的 `RA8P1_Project1` 独立复制而来，固定使用已经验证的 MCU/CN2 引脚，不改变硬件接口。

当前版本：`2026-07-30-production-ai-v2`

完整使用、安全边界、命令和引脚表见：

- [RA8P1_DAB_PRODUCTION_GUIDE.md](RA8P1_DAB_PRODUCTION_GUIDE.md)
- [RA8P1_AI_ACCELERATION_GUIDE.md](RA8P1_AI_ACCELERATION_GUIDE.md)

## 安全默认值

- 上电后 8 路 PWM 引脚、`RUN_REQ`、预充、主输入和输出保护全部关闭。
- 不自动传输功率。
- 必须有有效 ADC、健康硬件保护、有效电流零点和 60 秒内下发的功率命令，才能执行 `control on`。
- USB CDC 在运行中断开会锁存故障并关断全部功率路径。
- 故障后不会自动重启，必须排除原因、`clear`、重新 `zero`、重新下发功率。

## 最短启动流程

主功率断开、确认实际电流为零：

```text
status
zero
```

接通受限电源和负载后，从 20 W 开始：

```text
power 20
control on
```

任何时候停止：

```text
control off
```

本固件已通过 e² studio/FSP 6.4.0 完整编译，但编译不等于功率硬件验证。首次实物测试仍必须使用限流电源、示波器和硬件急停，并从 20 W 分级验证。
