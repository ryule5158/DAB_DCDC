# RA8P1 v46a 以太网实板首测记录（2026-08-18）

## 范围与安全边界

- 基线：`v46` CP + CAN 固件；本次只增加只读以太网状态诊断，版本为 `2026-08-18-dualcore-cp-can-ethdiag-v46a`。
- 未发送任何 UART 控制命令，未启动 PWM 或功率路径。
- 只下载 CPU0；CPU1 未重刷。
- 自动复位后的 UART 明确报告 `outputs/path OFF`、`[DAB][STATE] READY`，CANFD0 初始化为 PASS。

## 构建与下载

- CPU0：text/data/bss = `165452 / 560 / 172377`，ELF SHA-256 = `A346818F237B7FA3DEB28CEAE63AFBFF105E81CCD6E1CA5DE77B0DE8E23CF5A8`。
- CPU1 兼容性构建：text/data/bss = `16982 / 132 / 9104`，ELF SHA-256 = `B694A8609BCD12C19B65295F158C378271A96D4997DC45BB15C8C7B287AB8616`。
- J-Link CPU0 下载报告 Bank 0 共 167936 bytes，Erase/Program/Verify 完成，随后执行系统复位并运行。
- 固件归档：`artifacts/2026-08-18_v46a_ethdiag/`。

## 实板结果

启动捕获：

- `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-18_v46a_ethdiag_flash_boot.log`
- `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-18_v46a_ethdiag_delayed_link_check.log`

稳定重复的诊断行为：

```text
[ETHDIAG] task=1 reset_n=1 ipinit=1/1 ok=1 tcp=00000000
          rmac=524D4143 phy=52504859 phy_if=1 link=0 net=0
```

解释：

- Ethernet 任务已运行，外部 PHY 复位已经释放。
- `FreeRTOS_IPInit()` 已调用、已返回并成功创建 IP 栈。
- `524D4143` 为 RMAC 控制块打开标记，`52504859` 为 RPHY 控制块打开标记，PHY 接口状态为 initialized。
- FSP 的链路处理持续返回物理 Link Down，因此网络接口未进入 up，TCP 服务还不能监听。
- 延时复查时网络事件计数已超过 100，仍然是 `link=0/net=0`，排除仅仅是自协商启动慢。
- PC Realtek PCIe GbE 同步报告 `Media disconnected`；`ping 192.168.10.100` 100% 丢包，ARP 中没有板卡条目。由于链路层未建立，不能把该 Ping 结果解释为 IP 或 TCP 故障。

## EasyEDA 原生网表与官方参考对照

原生项目 `ProPrj_瑞萨杯_2026-08-09.epro2` 中：

- U7 LAN8720A 的 TXP/TXN/RXP/RXN 分别连接 J2 HR911105A 的 TD+/TD-/RD+/RD-；四线均带 49.9 ohm 到 `3V3_ETH_A`，并接 TPD4E05U06 ESD 器件。
- J2 PHY 侧两个中心抽头共接 `3V3_ETH_A`；RBIAS 为 12.1 kohm 到 GND。
- MODE[2:0] 为 10 kohm 上拉，选择全能力自协商；PHYAD0 为低，地址 0 与 FSP 配置一致。
- LED1/REGOFF 为 10 kohm 下拉，内部 1.2 V 稳压器启用；LED2/nINTSEL 为上拉，符合外部 50 MHz 振荡器用法。
- J2 自带 LED 引脚 9--12 均为 NC；链路/活动显示使用外置 LED6/LED7。这不会阻断 Link。
- 需现场复核的偏差：中心抽头去数字地的 C96 为 100 nF，而 Microchip LAN8720A 检查表参考值为 22 nF。该项目前仅列为复核点，不能单凭网表断言它造成 Link Down。

## 结论与下一验证门

当前故障边界在物理层：`LAN8720A MDI -> HR911105A -> 网线 -> PC 网卡`。固件任务、PHY 复位释放、FreeRTOS+TCP 创建和 RMAC/PHY 驱动打开均已越过；静态 IP、Ping 和 TCP/JSONL 双向通信必须等待 Link Up。

现场按以下顺序验证：

1. 重新插拔并更换一根已知良好的 Cat5e/Cat6 网线，或先接到交换机；观察外置 LED6/LED7，而不是 RJ45 自带灯。
2. 测 U7 的 VDD1A/VDD2A/VDDIO 是否约 3.3 V、VDDCR 是否约 1.2 V、nRST 是否稳定为高。
3. 示波器确认 U7 pin 5 有 50 MHz 时钟，并检查 TXP/TXN 是否有自协商脉冲。
4. 检查 U7、D3、J2 焊接与四条 MDI 网络连续性；必要时复核 C96 值。
5. Link Up 后再把 PC 有线口设为 `192.168.10.10/24`，依次执行 Ping、TCP 5000 和 JSON Lines 双向收发测试。
