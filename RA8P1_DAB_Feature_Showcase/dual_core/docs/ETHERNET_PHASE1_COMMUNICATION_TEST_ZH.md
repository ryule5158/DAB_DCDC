# Ethernet 第一阶段通信验证

本阶段只完成 PHY、Link、静态 IPv4、ICMP Ping、TCP 5000 和 JSON Lines 通信底座。PySide6/PyQtGraph GUI 暂不加入，等实板通信通过后再做，避免把链路问题和界面问题混在一起。

## 固件配置

- CPU0：FSP 6.4.0 `r_rmac` 通道 1、`r_rmac_phy` 通道 1、RMII、PHY 地址 0、外部 50 MHz 参考时钟。
- 网络：FreeRTOS+TCP，静态 IP `192.168.10.100`，掩码 `255.255.255.0`，网关/DNS `192.168.10.1`，MAC `02:52:A8:10:00:64`。
- ICMP Echo Reply 已启用；TCP Server 监听 `5000`，一次服务一个 PC 客户端。
- 接收器按换行分帧，支持一次 `recv` 中多行粘包和一行跨多次 `recv` 的拆包。单行上限 1023 字节；超长行丢弃到下一换行并返回错误。
- 每 1 秒主动发送 telemetry；15 秒没有收到客户端任何数据则发送 heartbeat 超时错误并断开。断线或 Link Down 后服务任务继续等待并重新监听，不影响 DAB 主控制任务。
- Ethernet 初始化或任务创建失败时，仅禁用网络路径；UART9、ADC/PWM、双核 IPC 和原 DAB 状态机继续运行。

## JSON Lines 协议

每个 JSON 对象以一个 `\n` 结束。客户端命令可用：

```json
{"id":1,"cmd":"ping"}
{"id":2,"cmd":"get_status"}
{"id":3,"cmd":"get_parameter","name":"power_limit_w"}
{"id":4,"cmd":"set_parameter","name":"load_mode","value":"resistive_load"}
{"id":5,"cmd":"start"}
{"id":6,"cmd":"stop"}
```

参数名：`power_limit_w`、`voltage_ref_v`、`kp_ns_per_v`、`ki_us_per_vs`、`phase_ns`、`modulation`、`load_mode`。

`load_mode` 支持 `resistive_load` 和 `battery_load`，本阶段只是上位机模式标签。状态中的 `load_mode_control_active=false` 表示它尚未改变 DAB 控制律；后续 GUI 可以围绕这个字段扩展，协议和 UI 不会被定死。

`start` 与 UART 的 `on` 调用相同的现有状态机，可能使功率级上电。`stop` 与 UART 的 `off` 一样立即关闭 PWM、RUN_REQ 和全部功率路径。当前镜像仍是原工程的 manual-stop-only 配置，以太网功能没有开启任何新的自动关断，也没有修改保护语义。

## PC 直连验证顺序

1. 只接 RJ45 与调试供电，功率输入保持断开。给 PC 网卡设置手动 IPv4 `192.168.10.10`、掩码 `255.255.255.0`，网关留空或保持与该直连口不冲突。
2. 烧录 CPU0/CPU1 后观察 PHY Link LED 和 UART 启动行；这是第一次实板验证，构建成功本身不能证明 Link。
3. 在 Windows 运行：

   ```powershell
   ping 192.168.10.100
   python .\tools\dab_tcp_smoke.py
   ```

4. 默认脚本不会发送 `start`，也不会修改任何功率控制参数。它依次验证 hello、拆包 ping、粘包双命令、安全的负载模式标签、telemetry、心跳 ping、主动断线和自动重连后的 ping。
5. 只有在明确允许功率级动作、现场具备限流和停机条件时，才使用：

   ```powershell
   python .\tools\dab_tcp_smoke.py --exercise-start-stop
   ```

建议 GUI 后续每 5 秒发送一次 `ping` 作为心跳，接收循环持续按 `\n` 分帧；连接断开后用有上限的退避重连。GUI 第一版再加入连接配置、状态、参数、阻性/电池模式选择、启动停止、实时曲线和 TX/RX 日志。

## 2026-08-15 构建与静态验收

- CPU0 已用 FSP 6.4.0 生成内容和工程自带 Renesas LLVM makefile 完成链接：`0 errors`，ELF 大小统计为 `text=111506`、`data=428`、`bss=169697` 字节。
- CPU0 全量图形化构建只出现 1 条 FSP 6.4.0 生成驱动告警：`r_rmac_phy.c` 的 `line_speed_duplex` 形参未使用。新增的 `dab_ethernet.c`、`dab_remote_control.c` 和修改后的 `User_Init_Task_entry.c` 单独重编译均为 0 告警；没有修改该供应商生成文件来隐藏告警。
- CPU1 增量构建为 `0 errors, 0 warnings`，其 Pins 生成仍关闭、无 `r_ioport`，没有加入 Ethernet 源文件或外设实例。
- 双核/保护/分区/Helium/Ethernet 静态验收命令已通过：

  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Verify-ReferenceParity.ps1 -RequireBuildArtifacts -AllowEthernetPhase1
  python -m py_compile .\tools\dab_tcp_smoke.py
  ```

`-AllowEthernetPhase1` 不是跳过核对：它要求参考工程原有 FSP 属性完整保留，只允许新增 RMAC/PHY/FreeRTOS+TCP 属性，并继续检查 CPU1 引脚隔离、分区、UART9、手动停机锁、POEG、Helium 和产物时间；同时新增检查 RMAC1/RMII/PHY@0/外部时钟、静态 IP、Ping、TCP 5000、JSONL 命令、粘拆包、心跳、重连及安全控制路径。

## 证据边界

- FSP 生成和编译：证明 API、引脚、链接和内存配置能组成镜像。
- PC 脚本离线检查：证明客户端语法和分帧逻辑可执行。
- Ping：证明二层链路、ARP、IPv4 和 ICMP 往返。
- TCP 冒烟通过：证明端口、JSONL 分帧、命令、telemetry 和重连。
- 只有实板抓取的 UART/PC 日志才能证明真实通信；本阶段不会访问 COM4、刷写或给功率级上电。

硬件映射证据见 [Ethernet 原生网表核对记录](ETHERNET_NATIVE_NETLIST_EVIDENCE_2026-08-15.md)。
