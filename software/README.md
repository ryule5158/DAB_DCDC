# 固件和上位机

- [RA8P1 双核固件](RA8P1_DAB_Feature_Showcase/dual_core/)包含 e² studio 工程、FSP 配置和源代码。CPU0 处理 PWM、同步采样与实时闭环，CPU1 运行能量管理。
- [串口上位机](RA8P1_DAB_UpperComputer/)是 Python 工程，入口为 app.py，用于查看遥测、记录测试数据和控制充电模式。运行依赖与命令见该目录的说明。
