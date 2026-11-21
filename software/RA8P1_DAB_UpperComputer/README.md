# RA8P1 DAB 智能充电 + MPPT 现场上位机

本上位机严格对应 MOS 更换后的融合测试计划，只通过 `COM4 / 115200 / 8N1` 串口连接实物板。

界面只保留一个交付功能：

1. 智能充电 + MPPT：只选择 10S 锂离子或 12S 磷酸铁锂；预充、CV 和结束阶段完全服从电池状态机，只在 CC 大功率阶段运行P&O或增量电导MPPT。

CP 能源路由因本轮时间不足且无法用少量点完成稳定性、输入扰动、功率边界和调制选择验收，已从交付界面移除；固件内部兼容代码不作为本轮已交付功能。精准电流、固定功率、独立稳压模式、独立 MPPT、铅酸/自定义电池、CAN、Ethernet、离线模拟、在线自动整定、工程解锁、原始底层命令和演示预设同样不在本轮界面中。上位机不发送充电目标电压/电流、energy模式、MPPT电压范围/步长/周期/功率上限、inner、cap、PI或运行时调参命令；AI固定为OFF。物理验收输入仍为20/22/24 V，固件本地控制裕量为17.5~26 V；状态机、CC阶段自动MPPT、参数束选择、调制过渡和闭环全部由下位机完成。

## 运行

首次使用串口时安装：

```powershell
python -m pip install -r .\requirements-optional.txt
```

在本目录启动界面：

```powershell
python .\app.py
```

也可以双击 `run_demo.bat`。文件名保留兼容性，但现在启动的是串口现场界面，不再启动模拟器。

运行回归：

```powershell
python .\app.py --self-test
```

## 现场操作顺序

1. 确认板端处于 READY、PWM OFF、FAULT=0，再连接 COM4。
2. 选择电池类型和MPPT算法；智能充电必须明确选择电池类型。
3. 点击“应用模式（不启动）”，确认版本、目标、算法、参数表和 AI OFF 状态。
4. 点击“启动功率”并确认。上位机只发一次高层 START，运行中不根据遥测拼接底层命令。
5. 观察 Vin/Vout/Iin/Iout/Pin/Pout、温度、fault、充电阶段、MPPT 目标、requested/effective 调制和参数表条目。
6. 连接后自动创建 JSONL 记录；每个功能完成后导出 CSV，STOP 仅发送当前功能的高层停止命令。

## 串口映射

| 功能 | 上位机下发内容 |
| --- | --- |
| 智能充电 + MPPT | `ai off`、电池profile、P&O/INC、`bat start/stop`；上位机不发送energy模式，`bat start`由下位机对两种锂电池自动选择融合模式；本地范围、step/interval/Pmax、CV、CC/预充/终止电流、MPPT生效阶段、状态与调制完全由下位机决定 |
“本地控制状态”页只观察计划内的宽输入参数选择和SPS/EPS/TPS自动过渡，不提供在线调参入口。“通信日志”页保留 JSONL 实时记录与 CSV 导出，便于每点追溯。

## 文件结构

```text
RA8P1_DAB_UpperComputer\
├─ app.py                     串口现场界面与自测入口
├─ run_demo.bat               兼容保留的双击启动文件
├─ dab_console\protocol.py    高层 UART 命令映射与遥测解析
├─ dab_console\transports.py  串口传输（其他类不在界面暴露）
├─ dab_console\ui.py          智能充电+MPPT界面、趋势、日志和 CSV 导出
└─ tests\                     协议、传输和界面范围回归
```
