# RA8P1 Ethernet 原生网表核对记录（2026-08-15）

本记录来自 EasyEDA Pro 原生项目数据导出，不以截图作为连通性证据。核对对象是 PCB 文档 UUID `2e443d39c78011f1`；MCU 连接器为 `CN2`，PHY 为 `U7 LAN8720A`，带磁性器件 RJ45 为 `J2 HR911105A`。

## MCU 到 PHY 的 RMII1 网络

| 功能 | CN2 引脚 | 原生网络名 | CPU0 引脚用途 |
| --- | ---: | --- | --- |
| RMII TXD1 | 30 | `TXD1` | RMII1 TXD1 |
| RMII TXD0 | 28 | `TXD0` | RMII1 TXD0 |
| RMII TX_EN | 26 | `TX_EN` | RMII1 TX_EN |
| PHY 复位 | 22 | `PHY_RESET_N` | P513，低有效 |
| MDC | 20 | `MDC` | RMII1 MDC |
| 50 MHz 参考时钟 | 15 | `REF50CK` | RMII1 REF50CK |
| MDIO | 11 | `MDIO` | RMII1 MDIO |
| RMII RXD1 | 9 | `RXD1` | RMII1 RXD1 |
| RMII CRS_DV | 7 | `CRS_DV` | RMII1 CRS_DV |
| RMII RX_ER | 5 | `RX_ER` | RMII1 RX_ER |
| RMII RXD0 | 3 | `RXD0` | RMII1 RXD0 |

CPU0 FSP 引脚表对应 P206/P306/P307/P310/P312/P805/P806/P905/P906/P907；CPU1 不生成引脚数据，避免重配 CPU0 的 RMII、UART9 或功率控制引脚。

## PHY 地址、时钟、复位与铜口链路

- `U7.10 RXER/PHYAD0` 通过 10 kΩ 下拉，启动绑带选择 PHY 地址 `0`。
- `X1` 是板上 50 MHz 有源振荡器，经 `R123 22 Ω` 驱动 `REF50CK`，因此 FSP 选择 RMII 外部参考时钟输入。
- `P513` 经 `R121 100 Ω` 驱动 `PHY_RESET_N`；固件上电安全初始化保持低电平，再满足 LAN8720A 上电等待后释放。
- LAN8720A 的 MDI 差分对继续连接到 `J2 HR911105A` 的磁性器件和 RJ45 触点，链路路径为 `RA8P1 RMII1 → U7 LAN8720A → J2 HR911105A → PC`。

## 配置结论

- 使用 RMAC 通道 `1`、`r_rmac`、`r_rmac_phy` 和 RMII，不使用旧 `r_ether` 架构。
- `r_rmac_phy` 选择 PHY 地址 `0`、RMII、外部 50 MHz 参考时钟、`ETHER_PHY_LSI_TYPE_DEFAULT`。该类型保留 LAN8720A 绑带配置并通过标准 Clause 22 寄存器完成复位、自协商和 Link 读取。
- MAC 地址固定为本地管理地址 `02:52:A8:10:00:64`。

这些结论只证明工程配置与原生网表一致；未烧录前不代表 PHY 已经实际建立 Link。
