# 2026-08-19 MOS 更换后功能回归工作记录

固件：`2026-08-19-dualcore-safephase-v47l`，`MANUAL_STOP`。  
板端：COM4，115200-8N1。  
电子负载：操作者独占；主机不访问 COM24，不读取、不写设置、不切换 LOAD。  
总计划：`../../05_MOS更换后全功能回归测试计划_2026-08-19.md` v8。

## 当前检查点：约150 W功率层P&O与AUTO选模原点复测

r106按“24 V/8 A/CV36 V”准备指令启动后，板端在停机空载仍只读到Vin约19.41 V，带载约19.08~19.12 V，因此该日志不能记作24 V实物点；按板端及先前标称20 V经验，应归为当前约20 V/8 A/CV36 V高一层候选工况。v49ad运行无fault、温度约39/37 ℃并完成AUTO选择EPS，但外层P&O的`Ptarget`只短时到36 W后一路退至0 W，桥仍靠已有积分/相移保持约58~59 W输入，未向源可用上限继续爬升，结论`FAIL_P_AND_O_WINDDOWN_DURING_MODULATION_TRIAL_V49AD_R106`。失败日志完整保存为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN24_ILIM8_CV36_v49ad_r106_po_p180_continuous.log`；文件名保留启动前指令，不作为实际Vin证据。

根因是外层P&O与SPS/EPS/TPS本地模式试探并行，模式试探造成的瞬时Pin下降被P&O误判为越过源MPP，重复反向把已验证功率命令擦除。v49ae将P&O最小步进改为4 W/250 ms，在CV33~36 V下等效约0.44~0.48 A/s，仍受CPU0独立双向`<=0.5 A/s`物理斜率限制；新增实测最佳Pin/对应功率命令记忆，普通调制扰动只允许在最佳命令下方做一次有界试探，`Vin<Vmin`的真实源坍塌仍释放记忆并允许向下重捕获。P&O继续作为默认/主算法，INC只保留公共逻辑代表性回归和异常诊断，不再逐点双跑。

v49ae的energy与phase-search断言源通过Renesas ARM C11 `-Wall -Wextra -Werror`编译，参数生成检查`29/13/2`；CPU0/CPU1完整构建text/data/bss=`185514/560/172633`、`17842/132/9120`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ae_mppt_best_command_floor/`，CPU0 ELF/SREC SHA256=`8D19D2B91E60F45F3F6BD98C643884540851529E539E5B29D5B34DCD16D94493`/`07B4936F3FE97CCFC5BE9FE8039094729758B066D8E98291C9AC7A636208ACDA`，CPU1=`F19DC3E606BBF7ECC80BB5B62DDDEAF5378586411E2F7712D7BDDCE18291862E`/`97D2084380D9F856A4F65A662B1F405106CD4714AE1F6ED51E7E45A8E256CC49`。J-Link双核编程与校验退出码0，CPU0更新153600 bytes、CPU1更新18432 bytes；刷写后COM4只读确认版本v49ae、READY、CTRL OFF、PWM0、四路路径关闭、fault0，本地P&O `18.5..26.0 V/0.20 V/250 ms/360 W`，日志`2026-08-20_v49ae_postflash_queryonly.log`。COM24始终未访问。当前必须在手动设置不变的实际约20 V/8 A/CV36 V原点r107复测，不能进入下一工况。

r107证明v49ae的P&O最佳命令保持修复有效：外层目标连续爬到`10 A/360 W`边界，AUTO试探期间不再退回低功率；但专用入口`33090001`的执行器持续饱和于SPS外相移`120 ns`，稳定板端约Vin=19.03~19.05 V、Iin=5.24~5.29 A、Pin=99.8~100.7 W、Vout=35.83~35.86 V、Iout=2.58~2.60 A，最高温度约44/34 ℃、fault0，未利用输入源8 A可用裕量，结论`FAIL_MPPT_EXECUTOR_PHASE_CAP120_SATURATION_V49AE_R107`。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM8_CV36_v49ae_r107_po_best_floor_continuous.log`；停止后COM4短查确认READY、PWM0、GPT停止、路径全关、fault0，日志`2026-08-20_v49ae_r107_fail_stop_queryonly.log`。捕获脚本结束时自身第148行把`$false`误写为`false`而退出1，但其TX-LIVE/TX-END均已发出`bat stop`，最终停机状态由独立COM4回查确认；不把脚本退出码当板端故障。

v49af只把融合CC/MPPT专用入口`33090001`相移包络从120 ns保守扩到240 ns；普通智能充电、预充、CP以及平台480 ns绝对边界均未改，电流参考`<=0.5 A/s`、inner`<=0.2 ns/ms`和连续PWM规则保持。严格ARM C11编译、参数`29/13/2`与双核构建通过，CPU0/CPU1 text/data/bss=`185554/560/172633`、`17842/132/9120`。产物`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49af_mppt_phasecap240/`；CPU0 ELF/SREC SHA256=`CC59DAAAEA4B9913042229DA0A13EB88660BB3031B5ED13F460B5CBFABEE1FC6`/`7F4A29F7EDF1C4BE666DD736A770D9D071E7ADA12FE5CA3AB9CF6DA1880C6B18`，CPU1=`F19DC3E606BBF7ECC80BB5B62DDDEAF5378586411E2F7712D7BDDCE18291862E`/`97D2084380D9F856A4F65A662B1F405106CD4714AE1F6ED51E7E45A8E256CC49`。当前状态`BUILT_AWAITING_FLASH_AND_R108_SAME_POINT_PHYSICAL_PASS`；仍锁定同一约20 V/8 A/CV36 V、AI OFF/P&O/AUTO原点。

v49af随后完成双核J-Link编程/校验，退出码0；CPU0 Bank0更新186368 bytes，CPU1内容已一致。冷启动只通过COM4查询并立即释放，确认版本v49af、READY、CTRL OFF、PWM0、GPT停止、路径全关、fault0，本地P&O仍为`18.5..26.0 V/0.20 V/250 ms/360 W`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49af_postflash_queryonly.log`。当前状态`FLASHED_AWAITING_R108_SAME_POINT_PHYSICAL_PASS`，COM24未访问。

r108在同一实际约20 V/输入限流8 A/CV36 V、AI OFF/P&O/AUTO原点验证v49af。过程连续PWM、无fault且没有异常电流突升；P&O目标保持在`10 A/360 W`，说明外环并未错误限功率。但模式搜索最终仍选SPS240 ns，`PinBest=105.00 W`、`etaBest=92.772%`，末段约Vin19.077 V、Iin5.618 A、Pin107.17 W、Vout35.952 V、Iout2.736 A，温度约45.33/34.23 ℃，未追到人工8 A源限流折点。EPS按旧比值公式只从约242/0 ns起步并落入47~53 W低功率盆地；TPS14/0~16 ns约103~104 W。结论`FAIL_MPPT_LOCAL_SEARCH_MISSES_PROVEN_HIGH_POWER_EPS_BASIN_V49AF_R108`，仍锁定原点。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM8_CV36_v49af_r108_po_phasecap240_continuous.log`；结束后独立确认READY/PWM0/路径全关/fault0，日志`2026-08-20_v49af_r108_fail_stop_queryonly.log`。COM24未访问。

历史同板v39的CV36实测已经给出可复用安全起点：EPS360/0 ns、cap320下Vin22.8785 V、Iin7.1844 A、Pin164.3677 W、Iout4.0007 A、Pout143.8692 W、温度53.6943/34.3922 ℃、fault0，实际effective/command约285/286 ns。v49ag据此只把融合MPPT搜索包络240→320 ns，并在支持的锂电CC变比区把EPS启动种子改为360/0 ns；它仍只是起点，后续按16 ns局部搜索，TPS第三相移仍由0开始逐步试探。`<=0.5 A/s`电流参考、`<=0.2 ns/ms`inner、连续PWM、360 W绝对上限和P&O外环均不变。严格ARM编译、参数29/13/2与双核构建通过；CPU0/CPU1 text/data/bss=`185618/560/172633`、`17842/132/9120`，产物与哈希已实时固化在`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ag_mppt_eps360_cap320/`。随后J-Link双核烧录/校验退出码0，CPU0更新186368 bytes、CPU1内容一致；冷启动COM4短查确认v49ag、READY/PWM0/GPT停止/路径全关/fault0与本地P&O `18.5..26.0 V/0.20 V/250 ms/360 W`，日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ag_postflash_queryonly.log`。当前状态`FLASHED_AWAITING_R109_SAME_POINT_PHYSICAL_PASS`，COM24未访问。

r109在同一实际约20 V/输入限流8 A/CV36 V、AI OFF/P&O/AUTO原点完成了SPS/EPS/TPS全流程，连续PWM、fault0且没有异常电流突变。结果仍FAIL：EPS360/0在当前`m=Vout/(2Vin)≈0.94`近匹配变比下只有约29~31 W；TPS约109 W；最终选择SPS、`PinBest=110.22 W`、`etaBest=92.339%`。选择后的591个RUN样本平均Vin18.7998 V、Iin5.9248 A、Pin111.3851 W、Vout35.5697 V、Iout2.8671 A、Pout101.9802 W，温度平均37.43/31.83 ℃、最高38.48/32.22 ℃，effective/command持续320/320 ns且fault0。P&O目标仍高于实测，输入仅5.9 A而不是人工8 A源折点，证明320 ns专用入口截断了仍在上升的SPS扫描；结论`FAIL_MPPT_NEAR_MATCH_FORCED_EPS_AND_CAP320_V49AG_R109`。日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM8_CV36_v49ag_r109_po_eps360_cap320_continuous.log`。结束经COM4高层`bat stop`，最终独立确认READY/PWM0/GPT停止/路径全关/fault0，日志`2026-08-20_v49ag_r109_fail_stop_final_queryonly.log`；COM24未访问。

v49ah按r109直接证据修复而不进入下一工况：融合MPPT专用入口使用平台已有480 ns绝对包络；普通充电、PRECHARGE、CP和360 W功率绝对上限不变。近匹配变比`0.86<=m<=1.14`时EPS从0 ns起步并只试+16 ns，使“一个相移足够”自然判为SPS；22/24 V对应的非近匹配区域仍保留历史360 ns EPS种子。电流参考`<=0.5 A/s`、inner`<=0.2 ns/ms`和连续PWM均不变，没有新增Iin/Iout停机阈值或点功率上限。严格ARM编译、参数29/13/2及双核构建通过；CPU0/CPU1 text/data/bss=`185658/560/172633`、`17842/132/9120`，镜像、哈希和刷写脚本已固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ah_mppt_match_sps_cap480/`。J-Link双核编程/校验退出码0，CPU0 Bank0更新186368 bytes、CPU1与产物一致；冷启动COM4短查确认v49ah、READY/CTRL OFF/PWM0/GPT停止/路径全关/fault0及本地P&O `18.5..26.0 V/0.20 V/250 ms/360 W`，证据`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ah_postflash_queryonly.log`。当前状态`FLASHED_AWAITING_R110_SAME_POINT_PHYSICAL_PASS`；COM24未访问。

r110在未改变的实际约20 V/源限流8 A/CV36 V原点执行AI OFF+下位机P&O+本地选模，确认480 ns融合包络正确加载、PWM连续、fault0。SPS扫描最佳实测样本为Vin18.72 V、Iin8.63 A、Pin161.62 W、Pout146.91 W、raw/effective/command约448/448/446 ns、inner0/0 ns；全部模式比较最终选中SPS并汇报`PinBest=161.20 W, etaBest=91.120%`。但现有`dab_mppt_phase_metric_t`只保存Pin/效率/内相移，没有保存该样本的外相移；HOLD因此继续使用扫描末端480 ns，错误稳在Vin约16.48 V、Iin约8.37 A、Pin约137.9 W，再次构成“找到后丢点”。结论`FAIL_MPPT_BEST_OUTER_PHASE_NOT_REPLAYED_V49AH_R110`。已通过COM4高层`bat stop`结束，最终查询确认v49ah、READY/CTRL OFF/PWM0/路径全关/fault0，停机Vin约19.44 V、温度约37.0/33.2 ℃。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM8_CV36_v49ah_r110_po_matchsps_cap480_continuous.log`。下一版必须保存并回放每个候选的真实outer phase，原点重测r111；COM24未访问。

v49ai已按r110直接证据完成修复：`dab_mppt_phase_metric_t`新增真实应用outer phase，每个瞬时候选和1 s平均bucket都将其与Pin/Pout/效率/内相移同步保存；选模完成后，中选outer phase作为融合外环HOLD cap，一旦持续工况变化触发重搜则自动恢复480 ns探索包络。这不是固定功率或工况写死，全程仍由下位机独立发现和回放；P&O主算法、AI OFF、0.5 A/s电流斜率、0.2 ns/ms inner和连续PWM不变。新增r110回归断言要求SPS448 ns候选进入HOLD后必须保持448 ns outer回放值。严格ARM phase+energy编译及参数生成检查`29/13/2`通过；CPU0/CPU1构建text/data/bss=`185874/560/172653`、`17842/132/9120`，最终CPU0构建无新警告。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ai_mppt_outer_replay/`，CPU0 ELF/SREC SHA256=`C3542CBA0785EC59CD65C8C6E4A54DE3FDC09957568C9F41F1D6D10076397E0C`/`646254A2C01DC4DA11F986025175412265D902969CBA2E90AF204B9F5FB26BDD`，CPU1=`F19DC3E606BBF7ECC80BB5B62DDDEAF5378586411E2F7712D7BDDCE18291862E`/`97D2084380D9F856A4F65A662B1F405106CD4714AE1F6ED51E7E45A8E256CC49`。J-Link双核编程/校验退出码0，CPU0 Bank0更新188416 bytes、CPU1内容一致；冷启动COM4短查确认v49ai、READY/CTRL OFF/PWM0/路径全关/fault0和本地P&O `18.5..26.0 V/0.20 V/250 ms/360 W`，日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_postflash_queryonly.log`。当前`FLASHED_AWAITING_R111_SAME_POINT_PHYSICAL_PASS`，COM24未访问。

r111在同一实际约20 V/源限流8 A/CV36 V原点通过：v49ai下位机自主完成SPS/EPS/TPS比较，选中`SPS outer=448 ns, inner=0/0 ns`，汇报`PinBest=163.83 W, etaBest=90.797%`。内相移平滑退回0后，最后150个DAB样本覆盖11.166 s，raw/effective/command最小/最大均为448/448 ns；平均Vin18.5476 V、Iin8.4310 A、Pin156.3741 W、Pout140.1371 W，Pin范围155.82~157.05 W，温度最高36.38/31.38 ℃，fault0。这明确关闭了r110的“找到后回到480 ns/16.5 V错点”失败，结论`PASS_MPPT_LOCAL_SPS_OUTER_REPLAY_HOLD_V49AI_R111`。结束经COM4高层`bat stop`，最终v49ai、READY/CTRL OFF/PWM0/路径全关/fault0，停机前温度约38.0/32.7 ℃。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM8_CV36_v49ai_r111_po_outerreplay_continuous.log`。固件已在正确参数版本上运行；下一手工工况为22 V/同8 A限流/CV36 V，操作者完成后必须保留完整10 s无仪器访问窗口。COM24未访问。

操作者已完成下一手工工况，主机先保留完整10 s不访问COM4/任何仪器的窗口；之后仅COM4空载查询确认板端Vin21.38 V，符合标称22.0 V，v49ai处于READY/CTRL OFF/PWM0/路径全关/fault0，温度34.2/30.7 ℃。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_r112_pretest_queryonly.log`。r112锁定22 V/同8 A源限流/CV36 V，仅下发AI OFF、LFP profile、SOLAR、P&O、日志/保存和高层start；COM24未访问。

r112实测通过：下位机自主完成三模式比较，选中`EPS outer=240 ns, inner=45/0 ns`，汇报`PinBest=176.93 W, etaBest=90.762%`。TPS→EPS过渡期电流/功率连续、PWM无空窗且fault0；过渡后最新150个DAB样本覆盖11.166 s，raw/effective/command恒为264/242/240 ns、实际inner44/0 ns，平均Vin20.6815 V、Iin8.4544 A、Pin174.8499 W、Pout156.9631 W，Pin范围174.14~175.53 W，温度最高49.17/33.53 ℃，fault0。结论`PASS_MPPT_LOCAL_EPS_OUTER_REPLAY_HOLD_V49AI_R112`。结束经COM4高层`bat stop`，最终v49ai、READY/CTRL OFF/PWM0/路径全关/fault0。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM8_CV36_v49ai_r112_po_outerreplay_continuous.log`。下一手工点为24 V/同8 A源限流/CV36 V，仍先留10 s不访问仪器的窗口；COM24未访问。

操作者已完成24 V/同8 A源限流/CV36 V手工工况，主机完整保留10 s无COM/仪器访问窗口。随后仅用COM4低频查询确认板端Vin=23.36 V，固件v49ai、READY/CTRL OFF、PWM0、四路路径全关、fault0，温度40.7/31.4 ℃。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_r113_pretest_queryonly.log`。r113锁定该点，只运行AI OFF、LFP profile、SOLAR、P&O、AUTO本地选模及高层`bat start`，由下位机自主比较SPS/EPS/TPS并回放最佳外/内相移；COM24不访问。

r113实测通过：v49ai在AI OFF下由下位机自主完成SPS/EPS/TPS比较，选中`TPS outer=64 ns, inner=234/180 ns`并汇报`PinBest=187.96 W, etaBest=90.375%`。连续inner过渡完成后，最新150个PWM=1样本覆盖11.166 s，实装raw/effective/command恒为92/64/64 ns、inner恒为236/180 ns、cap64 ns；平均Vin22.6062 V、Vout35.8270 V、Iin8.5263 A、Iout4.8287 A、Pin192.7479 W、Pout172.9985 W，Pin范围191.70~193.63 W，温度最高52.20/33.99 ℃，fault0。全程PWM连续，没有脚本底层调制命令或重复硬开关。结论`PASS_MPPT_LOCAL_TPS_OUTER_REPLAY_HOLD_V49AI_R113`。结束仅通过COM4高层`bat stop`，回读确认v49ai、READY/CTRL OFF/PWM0/RUN0、四路路径全关、fault0；COM4已释放，COM24未访问。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN24_ILIM8_CV36_v49ai_r113_po_outerreplay_continuous.log`。约160~193 W层20/22/24 V矩阵关闭；下一点保持24 V/CV36 V，把输入源限流人工提高到10 A形成约240 W层，仍只跑P&O+AUTO，失败锁点修复。

操作者已完成24 V/输入源限流10 A/CV36 V手工工况，主机完整保留10 s无COM/仪器访问窗口。随后仅COM4低频查询确认板端Vin=23.36 V、温度35.2/30.2 ℃，v49ai处于READY/CTRL OFF、PWM0、四路路径全关、fault0。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_r114_pretest_queryonly.log`。r114锁定约240 W相邻功率层，只运行AI OFF、LFP profile、SOLAR、P&O、AUTO本地选模和高层`bat start`；COM24不访问。

r114实测通过：v49ai在AI OFF下由下位机自主比较三模式并选中`EPS outer=196 ns, inner=61/0 ns`，汇报`PinBest=256.75 W, etaBest=88.402%`。最终150个PWM=1样本覆盖11.166 s，实装raw/effective/command恒为228/198/196 ns、inner恒为60/0 ns、cap196 ns；平均Vin22.6247 V、Vout35.8090 V、Iin11.1994 A、Iout6.1479 A、Pin253.3830 W、Pout220.1488 W，Pin范围252.47~254.54 W，温度最高56.57/33.39 ℃，fault0。模式展开和回放期间PWM连续，没有脚本底层调制命令或重复硬开关，结论`PASS_MPPT_LOCAL_EPS_OUTER_REPLAY_HOLD_253W_V49AI_R114`。结束仅通过COM4高层`bat stop`，回读确认v49ai、READY/CTRL OFF/PWM0/RUN0、四路路径全关、fault0；COM4已释放，COM24未访问。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN24_ILIM10_CV36_v49ai_r114_po_outerreplay_continuous.log`。下一点保持24 V/CV36 V并把输入源限流人工提高到12 A形成约300 W层；先以停机温度回读确认冷却充分，失败锁点修复。

操作者已完成24 V/输入源限流12 A/CV36 V手工工况，主机完整保留10 s无COM/仪器访问窗口。随后只用COM4低频查询确认板端Vin=23.37 V，v49ai处于READY/CTRL OFF、PWM0、四路路径全关、fault0；温度已由r114峰值56.57/33.39 ℃下降到48.2/32.1 ℃。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_r115_pretest_queryonly.log`。r115锁定约300 W相邻层，只运行AI OFF、LFP profile、SOLAR、P&O、AUTO本地选模和高层`bat start`；COM24不访问。

r115实测形成热边界下的折点稳定PASS：搜索阶段Pin≥295 W共80个DAB点，时间跨度19.559 s，峰值301.70 W；下位机最终自主选中`SPS outer=264 ns, inner=0/0 ns`并汇报`PinBest=299.88 W, etaBest=86.976%`。约300 W候选和最终回放均使用相同264 ns执行坐标，脚本没有下发底层参数。输入源持续进入12 A限流及线路压降后，同一坐标最终150个PWM=1样本覆盖11.166 s，raw/effective/command恒264/264/264 ns、inner0/0 ns、cap264 ns；平均Vin22.1820 V、Vout36.0186 V、Iin12.2025 A、Iout6.4701 A、Pin270.6757 W、Pout233.0444 W，Pin范围269.90~271.48 W，fault0。Iin始终贴住12.17~12.24 A折点，没有再次落到2 A错误平台，PWM和模式持续稳定。因此记`BOUNDARY_PASS_MPPT_12A_SOURCE_KNEE_STABLE_SPS264_V49AI_R115`，但299.88 W只能作为瞬态搜索峰值，`ground_truth_validated=false`，不得进入MPPT模型真值或正式可持续Pmpp标签。主侧温度从48.2 ℃起步并最终达到75.14 ℃，次侧37.94 ℃；取得必要11.166 s后立即高层停止，回读确认v49ai、READY/CTRL OFF/PWM0/RUN0、四路路径全关、fault0。COM4已释放，COM24未访问。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN24_ILIM12_CV36_v49ai_r115_po_outerreplay_continuous.log`。在散热余量重新确认前不进入360 W源边界。

用户在r115后明确新增硬要求：“这个功率已经很大了，后续测试不要再超过此功率”。执行边界固化为：后续实物板端`Pin<=271.5 W`，20~24 V人工输入源限流统一不超过10.0 A；12 A及更高限流不再使用，300/360 W计划点停止执行。固件内360 W仍只是电路额定绝对边界，不改成点功率闭环、不由上位机发送功率上限。r115停机约30 s后只读COM4回查温度52.1/33.7 ℃，READY/PWM0/路径全关/fault0，日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_r115_cooldown30s_queryonly.log`；COM24未访问。下一人工点降为22 V/10 A/CV36 V。

操作者已完成22 V/输入源限流10 A/CV36 V手工工况，主机完整保留10 s无COM/仪器访问窗口。随后仅COM4低频查询确认板端Vin=21.37 V，对应标称22 V；温度44.8/32.3 ℃，v49ai处于READY/CTRL OFF、PWM0、四路路径全关、fault0。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_r116_pretest_queryonly.log`。r116只运行AI OFF、LFP profile、SOLAR、P&O、AUTO本地选模和高层`bat start`，输入源限流不超过新硬上限10 A；COM24不访问。

r116实测通过：AI OFF下位机自主完成三模式搜索，整个日志的最大Pin只有191.72 W，不存在找到更高点后错误放弃的问题；最终选中`SPS outer=148 ns, inner=0/0 ns`并汇报`PinBest=185.97 W, etaBest=90.783%`。最后150个PWM=1样本覆盖11.166 s，raw/effective/command恒148/148/148 ns、inner0/0 ns、cap148 ns；平均Vin20.9818 V、Vout36.0669 V、Iin9.0866 A、Iout4.7470 A、Pin190.6529 W、Pout171.2107 W，Pin范围189.86~191.33 W，温度最高52.71/34.54 ℃，fault0。模式和功率稳定，PWM连续且明显低于271.5 W新硬上限，结论`PASS_MPPT_LOCAL_SPS_HOLD_VIN22_ILIM10_V49AI_R116`。结束仅通过COM4高层`bat stop`，回读确认v49ai、READY/CTRL OFF/PWM0/RUN0、四路路径全关、fault0；COM4释放，COM24未访问。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM10_CV36_v49ai_r116_po_outerreplay_continuous.log`。下一人工点只降低输入到20 V，限流保持10 A、电子负载保持CV36 V。

操作者已完成20 V/输入源限流10 A/CV36 V手工工况，主机完整保留10 s无COM/仪器访问窗口。随后仅COM4低频查询确认板端Vin=19.49 V，对应标称20 V；温度47.0/33.0 ℃，v49ai处于READY/CTRL OFF、PWM0、四路路径全关、fault0。本地MPPT范围18.5~26.0 V，预检有0.99 V带载压降余量；r117必须重点检查带载后是否触发低端边界重搜，失败则锁点修复。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ai_r117_pretest_queryonly.log`；COM24不访问。

r117在同一20 V/10 A/CV36 V点由v49ai下位机自主完成搜索并选中`SPS outer=472 ns, inner=0/0 ns`，报告`PinBest=209.66 W, etaBest=89.704%`；随后实际坐标能在约Vin18.11 V、Iin10.95 A、Pin198.5 W、Pout175.2 W保持，证明中选模式/坐标本身正确。但18.11 V低于旧本地18.5 V下限，CPU1每周期撤回功率命令，导致进入HOLD和稳定锚定过慢，并保留再次落入高低功率循环的条件。结论`FAIL_MPPT_NOMINAL20_BOARD18P1_BELOW_LOCAL_FLOOR_V49AI_R117`，点位锁定；已仅经COM4高层停止并确认READY/PWM0/路径全关/fault0，原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM10_CV36_v49ai_r117_po_outerreplay_continuous.log`，COM24未访问。

v49aj离线修复、参数固化与上位机同步已完成：CPU0/CPU1的独立MPPT控制下限统一为17.5 V，仅作为20 V标称点带载压降裕量，物理矩阵仍为20/22/24 V；r111~r116的模式、outer、inner、Pin和效率记录已编入CPU0，仅在实时结果等价时作为确定性择优参考，明显更好的实时MPP始终优先，r115的12 A边界记录禁止用作运行提示。`bat start`由下位机按两种锂电profile和P&O/INC自动选择`SOLAR_CHARGER`，且先入队模式、后入队START；MPPT仍只在CC阶段运行。上位机不再发送`energy solar`，只发送profile、算法和高层start/stop。phase/energy主机执行测试、ARM `-Wall -Wextra -Werror`、上位机29项回归和参数`29/13/2`检查均通过；CPU0/CPU1 text/data/bss=`186530/560/172653`、`17842/132/9120`，产物与哈希固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49aj_mppt_edge_hold_autofusion/`。当前`BUILT_AWAITING_FLASH_AND_R117_SAME_POINT_RETEST`；原20 V/10 A/CV36 V点必须复测PASS后才继续余下矩阵。

v49aj已完成双核烧录/校验并在原20 V/10 A/CV36 V点执行r118。主机只发送`ai off`、`bat profile lfp12`、`mppt po`、日志/保存和高层`bat start/stop`，没有发送`energy solar`、调制、inner、cap或点功率命令；下位机回报`[BAT][AUTO] ... energy=SOLAR_CHARGER; MPPT automatic only in CC`，进入CC后自主完成SPS/EPS/TPS比较，最终选中`SPS outer=472 ns, inner=0/0 ns`，`PinBest=211.79 W, etaBest=89.581%`。最长连续472/0/0稳定段1348点、按板端100 kHz序列计101.09 s，平均Vin18.461 V、Vout36.037 V、Iin10.961 A、Iout4.965 A、Pin202.351 W、Pout178.929 W，Pin范围201.03~203.75 W；主/次温度在该段为32.10~45.56/28.86~34.50 ℃，fault0。全过程峰值Pin208.63 W、峰值Iin11.217 A，均低于后续271.5 W板端硬上限；高层停止后READY/PWM0/RUN0/四路路径关闭。结论`PASS_MPPT_NOMINAL20_EDGE_HOLD_AUTOFUSION_V49AJ_R118`，旧18.5 V地板导致的功率撤回与2 A低点循环已关闭。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM10_CV36_v49aj_r118_edgehold_autofusion.log`，COM24从未访问。

r119按剩余计划尝试带载连续20→22 V输入变化。v49aj仍只接收AI OFF、LFP、P&O和高层start；在20 V标称点自主选中SPS472/0/0，板端约Vin18.51 V、Iin10.96 A、Pin202.8 W、Pout180.4 W、fault0。由于MPPT已把输入推到10 A源限流折点，台式电源进入CC，操作者改变面板电压设定时实际Vin无法变化，因此连续Vin阶跃的物理激励不存在。没有提高10 A限流，也没有访问COM24；主机发送高层停止后脚本正常关闭，独立回读READY/CTRL OFF/PWM0/fault0，停机板端Vin=21.47 V，证明22 V设定已在卸载后生效。结论`NOT_APPLICABLE_SOURCE_CC_PREVENTS_LIVE_VIN_STEP_R119`，不计MPPT失败；20/22/24 V独立10 A点已经分别由r118/r116/r114通过，禁止为同一结论重复耗时。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_AUTOVIN_20_22_24_ILIM10_CV36_v49aj_r119.log`。

转入CP前的源代码审查发现现有产品路径仍要求上位机发送`mod`和`sched auto`，且CP控制在Vout低于18 V时返回0 A，会导致CP负载冷启动建压自锁；该状态不拿硬件试错。下一版v49ak在下位机内按20~24 V/42~44 V邻域从已上电电流环记录中自主选择SPS/EPS/TPS种子，CP低压区以`Ptarget/Vnom`平滑建立电流，仍受50 W/s功率目标斜坡和0.50 A/s电流斜率约束；上位机CP界面删除调制选择，配置不再发送`mod`或`sched auto`。完成构建/刷写后从24 V/CP50 W低功率点开始。

产品范围再次收敛：上位机只保留“智能充电+MPPT”和“CP能源路由”，已删除精准电流入口和固定功率选项；协议层对`mppt_algorithm=fixed`直接拒绝。融合智能充电配置只发送`ai off`、电池profile、P&O/INC、日志/保存和高层start/stop，不再发送`mppt vmin/vmax/step/interval/pmax`，更不发送充电电压/电流或底层调制参数。上位机回归29/29通过，范围扫描确认产品界面/串口映射无精准电流、固定功率或MPPT范围下发残留。

r120执行串口上位机代表性联调时复现新的同点失败。上位机只发送高层LFP+P&O+start，板端自动进入`SOLAR_CHARGER/CC`，但标称22 V/限流10 A/CV36 V的输入被持续拉到约14.14 V、10.8 A；末段板端约Pin153 W、Pout131 W、TPS180/720 ns、effective phase482 ns、outer持续饱和且fault0。CPU1随后已把MPPT功率/电流参考撤到0，功率仍未卸载；日志还明确出现`[DAB][WARN] PWM update missed >100 times`。这不是电源设定错误，而是固件欠压恢复执行器自锁，结论`FAIL_UPPER_FMP_VIN22_RECOVERY_ORIENTATION_PWM_EDGE_STUCK_V49AJ_R120`。原始上位机日志`RA8P1_DAB_UpperComputer/logs/session_20260820_225647.jsonl`完整保留；操作者停止并断开上位机后，COM4独立回读确认v49aj、READY、PWM0、四路路径全关、Vin21.43 V、fault0，证据`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49aj_upper_r120_fail_safe_stop_query.log`。原点重新锁定，不能转CP。

根因是恢复函数在Vin塌陷跨过`Vout/(2Vin)=1`时，把已验证的主边高TPS832/180 ns翻转成180/832 ns；secondary目标超过720 ns普通硬件包络，同时旧480 ns有效相移仍存在，原子PWM边沿更新开始失败并保留最后高功率波形。v49al固定欠压恢复的物理桥方向为已由r93及两种电池预充使用过的主边高TPS832/180 ns；输入恢复后正常EPS/TPS搜索仍按实时变比决定方向。新增Vin14.2/Vout36回归断言防止再次翻转，0.5 A/s电流斜率、0.2 ns/ms MPPT inner、连续PWM、AI OFF和全部v49aj成功参数不变。

v49al离线门禁已完成：phase-search、energy-control、promoted-table主机测试通过，参数检查仍为29/13/2，CPU0/CPU1完整构建text/data/bss=`187362/560/172653`、`17842/132/9120`。镜像与哈希固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49al_mppt_recovery_orientation/`；CPU0 ELF/SREC SHA256=`F4F0E5A485C7A2DB1CD859ECA8F0012AFDC29A7B8E3895F42C7B84D1B0FD36FC`/`B475D50C952065C0179332DE4CBAA1B7A250884B38E7A4A0A0C5EAF831B74486`，CPU1=`33BE8100483B99F757E03E61177B5F2B06574876B4D86CC7ECC6905319FD0A46`/`EA1F5215129FCD745DB84AECEE15E426E4E6BA3C6E0D7D9267F7A0471B0DF8D0`。当前`OFFLINE_READY_AWAITING_FLASH_AND_R120_SAME_POINT_RETEST`；COM24未访问。

根据操作者“MPPT范围可以宽一些”的最新要求，同时保留先前“不做20 V以下融合充电”的边界，v49u把下位机冷启动MPPT默认范围由历史15~30 V改为20~26 V；step=0.20 V、interval=250 ms、360 W绝对电路边界也都由下位机默认持有。现阶段实物验收仍先覆盖20/22/24 V，26 V只作为裕量，未实测前不记PASS。新增冷启动默认值主机断言与原能源/相移搜索测试均退出0；CPU0/CPU1完整构建text/data/bss=`183722/560/172577`、`17682/132/9112`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49u_mppt_local_defaults/`；CPU0 ELF/SREC SHA256=`6965038BB49EFCB77CC294D54F285E99A8FD7D3A0C14DDBC92B420D996E99726`/`830F479480F064E81561F846793DAE3052351E5FDA8E980473EF339A50A97882`，CPU1=`7DB312C27DAA9852F0C8FAD4A3002E7263D5213EF6ECEA20CFFB5596A2A668A3`/`BB2E946FABB57968B25E93C79C1FA27848C3CF6CE2B7DB3CAE4BB78DAFC43777`。当前状态`OFFLINE_READY_NOT_PHYSICAL_PASS`，尚未刷写或启动功率，COM24未访问。

v49u随后完成双核刷写与校验，J-Link退出码0，CPU0 Bank0更新184320 bytes、CPU1 Bank0更新18432 bytes。刷写后只通过COM4短时查询并立即释放，明确回报版本`2026-08-20-dualcore-mppt-local-defaults-v49u`、MPPT `range=20.0..26.0V step=0.20V interval=250ms Pmax=360W`、AI deterministic fallback=LOCAL；DAB为READY、CTRL OFF、PWM0、GPT停止、PRE/MAIN/OUT/RESET_N/RUN_REQ全关、fault0，Vin/Vout约21.33/0.10 V。日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49u_mppt_local_defaults_dualcore_flash_console.log`和`2026-08-20_v49u_postflash_queryonly.log`。该证据只关闭“下位机冷启动默认范围与升级”门禁，不代表带功率MPPT PASS；COM24未访问。

r90启动v49u后，操作者在同一功率会话中手动调节输入电源电流，用于验证能否把输入工作点调至22 V；因此日志中的Vin由约21 V跌至18.5 V、Iin和Pin变化以及SPS→EPS过程混合了外部工况变化，不能用于固件稳态PASS/FAIL或训练数据。主机误把该变化解释为自主越界，在操作者报告“完成”后发送了一次高层`bat stop`；调节进行期间未停止，但不应据此判算法失败。该轮结论修正为`ABORTED_OPERATOR_INPUT_CURRENT_ADJUSTMENT_NO_ALGORITHM_VERDICT`，不修改固件、不进入下一工况，原始日志完整保留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_VIN22_ILIM2P5_CV33_v49u_r90_inc_phase_search_continuous.log`。停机回查确认v49u、READY、PWM0、路径关闭、fault0。下一轮沿用操作者调好的最终输入设置重测；以后操作者调输入电流期间保持同一功率会话，只在其明确结束后追加10 s稳态再判定。

r91保持同一连续功率会话，操作者把输入电源限流由2.5 A上调到5 A、再下调到最终2.7 A，用于验证能否追回22 V；主机在整个调节期间未停止功率、未访问COM24。操作者明确“完成”后又保存满10 s稳态。最终板端Vin约18.47~18.50 V、Iin约2.70~2.74 A、Pin约49.9~50.6 W、Vout约32.73~32.75 V、Iout约1.38~1.41 A，温度最高约35.0/31.7 ℃、fault0。与此同时能源层`Ptarget=0 W`、Iactive=0 A，但相移层仍完成TPS排名并最终保持EPS inner约246/0 ns；这证明即使上调电源限流也追不到22 V的根因是EPS/TPS自然传输无法卸载，而非2.5 A限制本身。结论`FAIL_FMP_VIN22_TRACKING_ZERO_REFERENCE_EPS_NATURAL_TRANSFER_V49U_R91`；原点锁定，高层停止后才离线修复。

v49v修复规则：Vin低于本地20 V下限时，立即取消当前模式排名，连续PWM请求退回SPS且两个inner目标归零；实际inner仍按既有不超过1 ns/ms变化并由外相移补偿，不用硬关PWM。只有Vin恢复到至少20.25 V、电流参考收敛且稳定门满足后，才从SPS重新开始观察；任何欠压样本不得进入SPS/EPS/TPS排名。新增主机断言覆盖首次越界不得推进、恢复后完整排名、排名完成后再次欠压必须退回SPS/zero-inner，phase-search与energy测试均退出0。CPU0/CPU1完整构建text/data/bss=`183866/560/172581`、`17682/132/9112`。产物目录`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49v_mppt_vin_unload/`；CPU0 ELF/SREC SHA256=`5B22635A9D531F06CA056B514733D3A199F5B3862B5FB9E440B42F4AD170A7CF`/`0BBF41D955718CDB4F88438ABCBE55C124D7100B819D3097A8C359077755EEF8`，CPU1=`7DB312C27DAA9852F0C8FAD4A3002E7263D5213EF6ECEA20CFFB5596A2A668A3`/`BB2E946FABB57968B25E93C79C1FA27848C3CF6CE2B7DB3CAE4BB78DAFC43777`。当前`OFFLINE_READY_NOT_PHYSICAL_PASS`，待原点刷写复测。

v49v随后完成双核刷写和校验，J-Link退出码0，CPU0 Bank0更新186368 bytes，CPU1内容一致；控制台日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49v_mppt_vin_unload_dualcore_flash_console.log`。刷写后只短时占用COM4发送`version/show/mppt show/bat show/ai show`并立即释放，确认运行版本`2026-08-20-dualcore-mppt-vin-unload-v49v`、MPPT默认`range=20.0..26.0V step=0.20V interval=250ms Pmax=360W`、AI deterministic fallback=LOCAL；DAB为READY、CTRL OFF、PWM0、GPT停止、PRE/MAIN/OUT/RESET_N/RUN_REQ全关、fault0，Vin/Vout约21.33/0.10 V，温度约31.3/29.2 ℃。查询日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49v_postflash_queryonly.log`。该记录只证明刷写及安全空闲门禁通过，带功率原点仍须r92实测；COM24未访问。

r92在操作者确认输入22.0 V、限流2.7 A、OUTPUT ON并保持电子负载CV33.0 V/LOAD ON后，先完整保留10 s无仪器访问窗口，再只发送AI OFF、LFP12、SOLAR、INC、日志/保存和高层`bat start`；COM24始终未访问。v49v确实在Vin低于20 V后退到stage1/SPS，并最终把raw/effective/command phase及两路inner全部降到0，查询回报`requested/effective=SPS`、`ph=0/0ns`，但实物仍稳定吸收约45.8~46.4 W：Vin约17.09~17.11 V、Iin约2.68~2.71 A、Vout约32.35~32.39 V、Iout约1.27~1.30 A，温度最高约32.5/30.7 ℃、fault0。高层停止后板端Vin回升至约21.38 V、功率接近0，证明该变比下SPS零相移本身存在自然传输地板，v49v不能恢复输入源。结论`FAIL_FMP_VIN22_SPS_ZERO_PHASE_NATURAL_TRANSFER_V49V_R92`；原始日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM2P7_CV33_v49v_r92_inc_vin_unload_continuous.log`，停止后READY/PWM0、fault0，不进入下一工况。

v49w依据r92同板证据，把欠压恢复波形由SPS零相移改成已在当前硬件使用过的TPS低占空基线：高压侧inner 832 ns、低压侧inner 180 ns。若SPS或EPS试探把Vin拉到20 V以下，本轮将该模式标记为不可行；通过TPS基线恢复到至少20.25 V且参考/稳定门满足后，直接尝试下一个自由度，不再重复用同一模式拉垮输入。转换继续使用既有inner不超过1 ns/ms渐变、外相移补偿和连续PWM；电流参考双向不超过0.5 A/s。固件版本`2026-08-20-dualcore-mppt-tps-recovery-v49w`。相位搜索和energy断言源均通过ARM C11 `-Wall -Wextra -Werror`编译；本机Renesas LLVM无Windows/WASM后端，因此不虚构主机运行PASS。参数一致性`29/13/2`，CPU0/CPU1完整构建text/data/bss=`184394/560/172585`、`17682/132/9112`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49w_mppt_tps_recovery/`，CPU0 ELF/SREC SHA256=`5CBCFA9EE4D7FA52277872F054831173E1D312241CD02B8D81E6348DF3C8E6C8`/`22AEC995158B9A905F60D95382D2AC8B84023C091E63FDD3B5DF69595070EF8B`，CPU1=`7DB312C27DAA9852F0C8FAD4A3002E7263D5213EF6ECEA20CFFB5596A2A668A3`/`BB2E946FABB57968B25E93C79C1FA27848C3CF6CE2B7DB3CAE4BB78DAFC43777`。当前`OFFLINE_READY_NOT_PHYSICAL_PASS`，必须刷写并复测同一原点。

v49w随后通过J-Link完成双核刷写，CPU0 Bank0更新186368 bytes，CPU1内容一致，脚本退出码0；控制台日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49w_mppt_tps_recovery_dualcore_flash_console.log`。刷写后只短时占用COM4查询并立即释放，确认版本`2026-08-20-dualcore-mppt-tps-recovery-v49w`、MPPT默认`20.0..26.0V/0.20V/250ms/360W`、AI本地确定性回退；DAB为READY、CTRL OFF、PWM0、GPT停止、四路功率路径关闭、fault0，Vin/Vout约21.33/0.11 V，温度约28.0/27.3 ℃。查询日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49w_postflash_queryonly.log`。该证据仍仅关闭升级及安全空闲门禁，下一步固定原点r93实测；COM24未访问。

r93证明v49w欠压恢复修复有效，但MPP锁定失败。操作者在同一连续功率会话中手动调输入电源电流上限，主机没有发送停止或任何其他串口命令，也没有访问COM24；操作者确认完成后再完整保留10 s才发送一次高层`bat stop`。搜索多次在SPS阶段爬到约4.5~5.0 A、Pin约95~105 W，电源面板电压可顶到22 V，随后正确完成EPS/TPS比较；但每次选回SPS时，物理波形仍处于从EPS/TPS平滑回切的瞬态，实测Pin暂时仅约44 W。旧HOLD逻辑立即用该44 W与先前SPS最佳约100 W比较，超过15%阈值后马上`begin`重搜，于是形成操作者观察到的约4.6 A→2.1 A→4.6 A周期。故障不是找不到MPP，而是选中后使用跨模式旧样本触发假工况变化。结论`FAIL_FMP_MPP_FOUND_BUT_FALSE_RESEARCH_CYCLE_V49W_R93`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM2P7_CV33_v49w_r93_inc_tps_recovery_continuous.log`完整保留。全程fault0，最高温度约44.8/35.3 ℃；停止后READY、PWM0、功率接近0。

v49x针对r93形成锁定修复：单模式观察由10 s缩至6 s、模式间稳定由3 s缩至2 s，完整SPS/EPS/TPS名义排名由约36 s缩至约22 s；扰动仍为1 s。选出最佳模式后先给10 s回切和0.5 A/s电流爬升宽限，待参考收敛后以真实稳态Vin/Vout/Pin建立锁点，不再与其他模式的旧最佳样本直接判工况变化。锁定后只有电压变化超过1.0 V或Pin变化超过35%，并连续保持5 s，才允许重新搜索；短时扰动与正常MPPT摆动不重启。inner不超过1 ns/ms、连续PWM、外相移补偿、20~26 V、360 W绝对边界和AI OFF本地运行均保持。版本`2026-08-20-dualcore-mppt-lock-v49x`。相位搜索/energy断言源通过ARM C11严格编译，参数一致性`29/13/2`，CPU0/CPU1完整构建text/data/bss=`184530/560/172601`、`17682/132/9112`。产物目录`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49x_mppt_lock/`；CPU0 ELF/SREC SHA256=`74A010926D5A524984AA59CC697E542B716337BC14C0837952E9A10D3ECB6766`/`46893E8E183FD6304B6B4823293A061F01F766A8CF77D57F881CED478A45D870`，CPU1=`7DB312C27DAA9852F0C8FAD4A3002E7263D5213EF6ECEA20CFFB5596A2A668A3`/`BB2E946FABB57968B25E93C79C1FA27848C3CF6CE2B7DB3CAE4BB78DAFC43777`。当前`OFFLINE_READY_NOT_PHYSICAL_PASS`，必须原点复测一次搜索后稳定保持。

v49x随后完成双核烧录和校验：J-Link退出码0，CPU0 Bank0更新186368 bytes，CPU1内容与产物一致；原始控制台日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49x_mppt_lock_dualcore_flash_console.log`。烧录后只短时占用COM4发送`version/show/mppt show/bat show/ai show`并立即释放，确认版本`2026-08-20-dualcore-mppt-lock-v49x`、MPPT冷启动默认`20.0..26.0 V/0.20 V/250 ms/360 W`、AI OFF本地确定性回退；DAB为READY、CTRL OFF、PWM0、GPT停止、四路功率路径关闭、fault0，Vin/Vout约21.33/0.10 V，温度约33.8/30.5 ℃。查询日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49x_postflash_queryonly.log`。该记录只证明升级及安全空闲门禁通过，带功率锁定仍须r94原点实测；COM24未访问。

r94保持操作者调好的输入22 V/大于5 A限流折点与电子负载CV33 V/LOAD ON，完整10 s无仪器访问后仅通过COM4启动v49x、AI OFF、LFP12、SOLAR、INC和高层`bat start`；全程未访问COM24。搜索阶段曾达到板端Iin约5.4~6.0 A、Pin约109~124 W，最终报告EPS最佳Pin=109.79 W/eta=90.616%；但回切锁定后实际长时稳在Vin约21.11 V、Iin约2.60~2.64 A、Pin约55 W、Iout约1.42~1.44 A，与操作者确认的大于5 A最大限流点明显不符。结论`FAIL_FMP_FALSE_STABLE_2P64A_REPLAYED_TARGET_NOT_APPLIED_PHASE_V49X_R94`。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM4P6_CV33_v49x_r94_inc_lock_continuous.log`完整保留（文件名的ILIM4P6为启动前假设，操作者随后明确实际上限为5 A以上）。停止后回读READY、PWM0、路径关闭、fault0，Vin约21.37 V，日志`2026-08-20_FMP_v49x_r94_postfail_queryonly.log`。

v49y修复r94的根因：正常内相移实际只以0.04 ns/ms变化，而旧搜索器把“目标内相移”当成已施加值记录；经过约300 ns有用区域时测到109 W，却被错记为EPS653 ns，回放653 ns后只剩55 W。新逻辑从PWM实际斜率命令记录两路applied inner，EPS/TPS过渡轨迹中的有用中间点可作为可回放最优值；SPS只在两路实际inner接近0时取样。锁定前还必须在实际inner和电流参考收敛后，连续3 s复现至少选中Pin的80%；否则不得建立错误锁点，平滑重搜。MPPT专用inner斜率为0.2 ns/ms，仍低于1 ns/ms硬上限；电流参考0.5 A/s、连续PWM和外相移补偿保持。新增断言源与energy源通过ARM C11`-Wall -Wextra -Werror`编译，参数检查`29/13/2`，CPU0/CPU1完整构建text/data/bss=`185594/560/172621`、`17682/132/9112`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49y_mppt_replay/`，CPU0 ELF/SREC SHA256=`0A0C50195C2BF64BE853E863C67CB5875DB1DCD67515FA54FF61193094355854`/`5D749A7CF8F4D1FBB0995241A782B00E0CDD0B54FA90656FD5C3E9A7BE45EC3`，CPU1=`7DB312C27DAA9852F0C8FAD4A3002E7263D5213EF6ECEA20CFFB5596A2A668A3`/`BB2E946FABB57968B25E93C79C1FA27848C3CF6CE2B7DB3CAE4BB78DAFC43777`。当前`OFFLINE_READY_NOT_PHYSICAL_PASS`，必须同点r95复测。

v49y已通过J-Link完成双核烧录与校验，CPU0 Bank0更新186368 bytes、CPU1内容一致，退出码0；原始控制台日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49y_mppt_replay_dualcore_flash_console.log`。刷写后只短时占用COM4查询并释放，确认版本`2026-08-20-dualcore-mppt-replay-v49y`、MPPT默认`20.0..26.0 V/0.20 V/250 ms/360 W`、AI OFF本地回退，READY、CTRL OFF、PWM0、四路路径关闭、fault0，Vin/Vout约21.33/0.11 V，温度约29.8/28.7 ℃。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49y_postflash_queryonly.log`；COM24未访问。升级门禁通过不等于带功率PASS，下一步仅复测r95同一原点。

r95同点启动v49y后，applied-inner回放链路正确：搜索器选中EPS713/1 ns，实际PWM稳在712/0 ns，没有再发生“记录653 ns却在300 ns测功率”的错配。但第一轮在板端仅Iin约2.05~2.08 A、Pin约43.0~43.6 W时就完成排名，选中PinBest=49.21 W/eta=86.864%，随后稳定约15 s仍只有Vin约20.97 V、Iin约2.06 A、Pin约43.3 W、Iout约1.10 A。根因是WAIT_SOURCE_PEAK把低功率TPS波形上的outer PI饱和持续2 s错当成源峰值，而TPS本身在120 ns外相移上限下无法把电流推到5 A以上。结论`FAIL_FMP_PREMATURE_SOURCE_PEAK_FROM_OUTER_SATURATION_V49Y_R95`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM5PPLUS_CV33_v49y_r95_inc_replay_continuous.log`保留，停止前fault0，COM24未访问。

v49z删除“outer_saturated=MPP”条件，并把源峰值发现起始波形由低功率TPS改为单自由度SPS平滑探测。只有Vin接近20 V下边界或INC功率目标确认反向才算找到源折点；触发后使用已验证TPS832/180 ns连续恢复，再比较SPS/EPS/TPS。v49y的实际inner记录、80%/3 s回放验证、0.5 A/s电流斜率、0.2 ns/ms MPPT inner斜率均保留。ARM严格编译和参数`29/13/2`通过，CPU0/CPU1完整构建text/data/bss=`185506/560/172617`、`17682/132/9112`。产物`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49z_mppt_source_probe/`；CPU0 ELF/SREC SHA256=`9891B3282BA1F1FE18F9E8D80E84477B7DDDFB5B4B2DAC3054CF6CF8AF4DB079`/`BED9CB74EE1A0A68A336CA1B59C3525BF1011ECCECDEFECA6186083A68E7F244`，CPU1=`7DB312C27DAA9852F0C8FAD4A3002E7263D5213EF6ECEA20CFFB5596A2A668A3`/`BB2E946FABB57968B25E93C79C1FA27848C3CF6CE2B7DB3CAE4BB78DAFC43777`。当前`OFFLINE_READY_NOT_PHYSICAL_PASS`，同点r96复测。

v49z双核烧录与校验完成：CPU0 Bank0更新186368 bytes、CPU1内容一致，J-Link退出码0，日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49z_mppt_source_probe_dualcore_flash_console.log`。烧录后COM4短查确认版本`2026-08-20-dualcore-mppt-source-probe-v49z`、READY、CTRL OFF、PWM0、四路路径关闭、fault0，Vin/Vout约21.32/0.10 V，温度约30.9/29.3 ℃，MPPT冷启动仍为20.0..26.0 V/360 W。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49z_postflash_queryonly.log`；COM24未访问。升级通过，带功率尚待r96。

r96证明v49z的源峰值探测已生效：板端实测到Vin=20.664 V、Iin=5.475 A、Pin=113.14 W、Pout=102.41 W，温度约30.7/29.2 ℃、fault0；当时实际inner约388/0 ns。但该源探测样本没有写入模式候选，随后SPS欠压回退又清空全部指标，最终仍选中EPS PinBest=49.78 W并稳在约2 A。结论`FAIL_FMP_SOURCE_PEAK_5P475A_FOUND_THEN_DISCARDED_V49Z_R96`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM5PPLUS_CV33_v49z_r96_inc_source_probe_continuous.log`完整保留，COM24未访问。

v49aa把源探测过程的每个实际applied-inner波形归类为SPS/EPS/TPS候选：一路inner为零的388/0 ns归为EPS，因此r96的113 W点会保留。某一模式欠压时只清除该模式，不再清空其他可行源峰值；进入EPS/TPS排名时也不再覆盖已存候选。最终113 W候选仍必须通过80%/3 s实物回放检查才能锁定。ARM严格编译、参数`29/13/2`和双核链接通过，CPU0/CPU1 text/data/bss=`185490/560/172617`、`17682/132/9112`。产物`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49aa_mppt_source_keep/`；CPU0 ELF/SREC SHA256=`27F51B6B5608E8500F1976DA211C25A0409C6ACBC0FEA1D50D1111251B0B5874`/`BD380F1B2F5EEAC68573D45C0FEA11AB19A5E5AE075D42C416532D4C92D6355B`，CPU1哈希与v49z一致。当前`OFFLINE_READY_NOT_PHYSICAL_PASS`，同点r97复测。

v49aa已完成双核烧录，冷启动短查确认版本正确、READY、CTRL OFF、PWM0、路径关闭、fault0。r97保持输入22 V、限流5 A以上和电子负载CV33 V/LOAD ON，完整10 s无访问后只通过COM4启动AI OFF、LFP12、SOLAR、INC和高层`bat start`，COM24始终未访问。板端实际再次达到Iin=5.473 A，最高Pin=113.55 W，Vout约32.9 V，实际inner约400/0 ns，fault0；但最终仍选中EPS697/0 ns、PinBest=51.08 W，说明v49aa失败。根因是启动初期一次INC目标反向过早把状态从WAIT推进到SPS观察，真正的5 A以上峰值出现在后续SPS观察轨迹，旧记录函数已停止执行。结论`FAIL_FMP_REAL_113P55W_PEAK_OCCURS_AFTER_EARLY_STATE_ADVANCE_V49AA_R97`；连续日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM5PPLUS_CV33_v49aa_r97_inc_source_keep_continuous.log`保留，发现失败后只发送高层`bat stop`。

v49ab把候选捕获扩展到完整源发现及SPS/EPS/TPS比较轨迹，按PWM实际applied inner归类，并只接受Vin不低于20 V的样本；模式试探欠压只跳过后续试探，不再删除先前在有效输入范围内测得的候选。INC目标反向只有在电流参考收敛且源发现至少2 s后才允许触发，20.15 V真实输入边界仍可立即触发。新增断言复现“状态已经进入SPS观察后才出现113.55 W/400 ns”和“后续EPS欠压不得清除该候选”。相位搜索与energy测试通过ARM C11严格编译，参数检查`29/13/2`；CPU0/CPU1完整链接text/data/bss=`185482/560/172617`、`17682/132/9112`。产物`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ab_mppt_trajectory_keep/`；CPU0 ELF/SREC SHA256=`080DCA8DFA1317D0F23B32F53C4FCE0E5A53C2F73F230F2831B0DB7C912383D0`/`2373ED8CE4FBF813225D128506D4FB641A8CE192A5DC62634A7B836E3052FA26`，CPU1=`7DB312C27DAA9852F0C8FAD4A3002E7263D5213EF6ECEA20CFFB5596A2A668A3`/`BB2E946FABB57968B25E93C79C1FA27848C3CF6CE2B7DB3CAE4BB78DAFC43777`。当前`OFFLINE_READY_NOT_PHYSICAL_PASS`，必须同点r98复测。

v49ab已通过J-Link完成双核烧录和校验，CPU0 Bank0更新186368 bytes、CPU1内容一致，退出码0；控制台日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ab_mppt_trajectory_keep_dualcore_flash_console.log`。烧录后仅短时占用COM4查询并立即释放，确认版本`2026-08-20-dualcore-mppt-trajectory-keep-v49ab`、READY、CTRL OFF、PWM0、四路功率路径关闭、fault0，Vin/Vout约21.33/0.11 V，温度约28.5/27.7 ℃；MPPT冷启动仍为本地20.0..26.0 V/0.20 V/250 ms/360 W，AI为LOCAL/OFF。查询日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ab_postflash_queryonly.log`。该门禁只证明升级与安全空闲，下一步同一输入22 V/限流5 A以上/CV33 V原点执行r98；COM24未访问。

r98同点验证通过。完整10 s无访问后，仅由高层命令启动v49ab、AI OFF、LFP12、SOLAR、INC；下位机自主完成SPS/EPS/TPS搜索和平滑过渡。搜索最高到Iin=5.49 A、Pin=114.06 W，并最终选择EPS inner=388/0 ns、PinBest=113.53 W、etaBest=91.397%。回放后没有再跌回2 A，也没有再次进入stage1；最后40个连续样本Iin均值5.204 A、范围5.170~5.226 A，Pin均值109.06 W、范围108.35~109.55 W，Vout约32.92~32.97 V，inner始终388/0 ns。全程最高温度39.43/30.47 ℃、fault0。结论`PASS_PSEUDO_PV_MPP_RETAIN_REPLAY_HOLD_V49AB_R98`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM5PPLUS_CV33_v49ab_r98_inc_trajectory_keep_continuous.log`。结束只发送高层`bat stop`，停机短查版本仍为v49ab，Vin/Vout约21.38/0.10 V、功率近零，日志`2026-08-20_FMP_v49ab_r98_postpass_queryonly.log`。COM24始终未访问，电子负载未被写入或切换。

r99保持同一22 V/限流5 A以上/CV33 V工况，改用P&O高层选择后重复验证，仍由下位机独立搜索和选模。搜索最高Iin约5.48 A、Pin=114.46 W，最终选择EPS388/0 ns、PinBest=113.25 W、etaBest=91.358%；最后40个连续样本Iin均值5.411 A、范围5.390~5.431 A，Pin均值111.83 W、范围111.41~112.25 W，Pout均值101.19 W，温度末窗最高40.54/31.31 ℃，fault0且无stage1重搜。结论`PASS_PSEUDO_PV_PO_RETAIN_REPLAY_HOLD_V49AB_R99`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN22_ILIM5PPLUS_CV33_v49ab_r99_po_trajectory_keep_continuous.log`。结束只发送高层`bat stop`，COM24未访问。至此当前22 V/当前源限流折点的INC与P&O均通过，下一相邻工况按计划由操作者把输入改为24.0 V，电子负载继续CV33 V/LOAD ON。

r100在操作者把输入源改为24.0 V、限流保持5 A以上且电子负载继续CV33 V/LOAD ON后，先完整留出10 s无仪器访问窗口；随后仅通过COM4发送AI OFF、LFP12、SOLAR、INC、日志/保存和高层`bat start`，COM24始终未访问。搜索轨迹最高观察到Vin=22.87 V、Iin=5.45 A、Pin=124.69 W、Pout=111.53 W、实际inner约520/0 ns；下位机最终选择EPS471/0 ns、PinBest=120.46 W、etaBest=90.107%。回放超过80%门槛并稳定保持，最后40个连续样本Vin均值21.856 V、范围21.848~21.866 V，Iin均值5.396 A、范围5.374~5.424 A，Pin均值117.94 W、范围117.42~118.53 W，Pout均值104.31 W、温度范围41.43~41.77/30.45~30.68 ℃；没有再次进入stage1，周期状态回报`fault=00000000`。结论`PASS_PSEUDO_PV_INC_RETAIN_REPLAY_HOLD_V49AB_R100`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN24_ILIM5PPLUS_CV33_v49ab_r100_inc_trajectory_keep_continuous.log`。结束只向板端发送高层`bat stop`并释放COM4，未访问或切换电子负载；下一步在相同物理工况验证P&O。

r101保持同一输入24.0 V/限流5 A以上/CV33 V工况，板端停止后留出10 s静置，再只通过COM4把算法高层选择改为P&O并启动；COM24始终未访问。搜索最终选择EPS521/0 ns、PinBest=124.05 W、etaBest=89.595%，回放末段实际inner量化为520/0 ns；最后40个连续样本Vin均值22.33 V、范围22.32~22.34 V，Iin均值5.39 A、范围5.36~5.42 A，Pin均值120.40 W、范围119.70~120.95 W，Pout均值106.55 W、范围106.17~106.91 W，最高温度48.74/31.81 ℃。选择后仅保持stage7，没有重新进入stage1，周期状态回报`fault=00000000`。结论`PASS_PSEUDO_PV_PO_RETAIN_REPLAY_HOLD_V49AB_R101`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN24_ILIM5PPLUS_CV33_v49ab_r101_po_trajectory_keep_continuous.log`。结束只向板端发送高层`bat stop`并释放COM4，未访问或切换电子负载。至此24 V当前源限流折点的INC与P&O门禁均关闭；下一点按既定矩阵由操作者手动把输入改为20.0 V，其余设置不变。

r102在操作者把输入源手动改为20.0 V、限流仍高于5 A且电子负载保持CV33 V/LOAD ON后，完整留出10 s无仪器访问，再只通过COM4启动AI OFF/LFP12/SOLAR/INC。标称20 V在带载和板端ADC上仅约19.21~19.25 V；v49ab固定要求候选Vin≥20.0 V、恢复Vin≥20.25 V，同时energy MPPT目标下限也是20.0 V，因此下位机立即进入TPS832/180恢复束并把取能降到Iin约0.62~0.66 A、Pin约12 W，始终不能开始SPS/EPS/TPS排名。温度约41.5/32.6 ℃、周期状态`fault=00000000`。结论`FAIL_NOMINAL_VIN20_ADC_DROOP_BELOW_LOCAL_MIN_V49AB_R102`，日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM5PPLUS_CV33_v49ab_r102_inc_trajectory_keep_continuous.log`；结束只发送高层`bat stop`，COM24未访问。该20 V/INC原点锁定，不进入P&O。修复方向是在下位机冷启动MPPT范围和相位搜索有效边界中加入标称20 V的带载余量，使上位机仍不下发范围；物理验收矩阵仍只执行20/22/24 V，360 W绝对边界、0.5 A/s电流斜率和连续PWM不变。

v49ac把下位机energy MPPT冷启动下限和相位搜索有效下限统一改为18.5 V，恢复门槛为19.0 V；该1.5 V余量只覆盖标称20 V在板端ADC与线路带载下的偏差，不新增18.5 V人工测试点，上位机仍不发送范围。360 W绝对边界、0.5 A/s电流斜率、0.2 ns/ms inner斜率和连续PWM不变。新增nominal20=19.2 V仍可留在SPS发现路径的断言，phase-search与energy测试通过Renesas ARM C11严格编译，参数检查`29/13/2`；CPU0/CPU1构建text/data/bss=`185498/560/172617`、`17682/132/9112`。产物`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ac_mppt_nominal20_margin/`，CPU0 ELF/SREC SHA256=`AD27BB84884D4D7994C2E14079651A386BDC7209FA8C6CABE2CA5BA0F9066912`/`A70FE67817700D39FAE76EFABFE1049F1294FD279905874EDFAB6992708EF434`，CPU1=`BD7FBE6F0C00212CA225B3612343B8A0CBAB89E3FC431E51C4F33C70F9811A90`/`1EB790872379E996D367128C3FA5A660DDD8B8F91044579FD9012F058789E1CB`。J-Link双核刷写退出码0，CPU0 Bank0更新186368 bytes、CPU1 Bank0更新18432 bytes并完成校验；控制台日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ac_mppt_nominal20_margin_dualcore_flash_console.log`。刷写后仅COM4短查并释放，确认v49ac、MPPT本地18.5..26.0 V/0.20 V/250 ms/360 W、READY/CTRL OFF/PWM0/GPT停止、四路功率路径关闭、fault0；查询日志`2026-08-20_v49ac_postflash_queryonly.log`。以上只关闭构建、升级与安全空闲门禁，20 V/INC仍须原点r103实测；COM24未访问。

r103原点证明v49ac低端余量修复有效但锁定稳定性仍失败。下位机从板端Vin约19.2 V继续平滑提高取能，完整执行SPS/EPS/TPS比较，搜索达到Iin约5.37 A、Pin约101 W并首次选中EPS约11/7 ns、PinBest=100.83 W、etaBest=91.253%，fault0；但回放后Vin在18.50 V搜索下限附近轻微抖动，通用“非WAIT阶段低于下限立即回撤”分支在HOLD阶段也被触发，单个边界样本立刻清空候选并重搜。随后功率跌回Iin约0.8 A、Pin约15 W。结论`FAIL_NOMINAL_VIN20_MPP_FOUND_BUT_HOLD_BOUNDARY_CHATTER_RESEARCH_V49AC_R103`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM5PPLUS_CV33_v49ac_r103_inc_nominal20_margin_continuous.log`，结束只发送高层`bat stop`，COM24未访问。原点继续锁定。下一修复只让HOLD阶段使用已有5 s持续工况变化确认；SPS/EPS/TPS试探阶段仍保留低于18.5 V立即回撤，功率/斜率/连续PWM规则不变。

v49ad把HOLD阶段从“非WAIT低于18.5 V立即回撤”分支中排除，使其统一使用原有5 s持续工况变化确认；SPS/EPS/TPS模式试探仍在低于18.5 V时立即进入平滑恢复。新增断言要求18.49 V单点继续HOLD，仅当Vin持续17.6 V超过5 s才重搜。严格编译与参数检查`29/13/2`通过；CPU0/CPU1构建text/data/bss=`185514/560/172617`、`17682/132/9112`。产物`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ad_mppt_hold_floor_debounce/`，CPU0 ELF/SREC SHA256=`7804EBE6C12E435C600F1D8A263082C14FDE36AF74A43F781EF6A4048C06442E`/`1DB428B259C8994A882034A8C0F659E2BA06C6EBDD8FC698A9DB4969F298A61E`，CPU1=`BD7FBE6F0C00212CA225B3612343B8A0CBAB89E3FC431E51C4F33C70F9811A90`/`1EB790872379E996D367128C3FA5A660DDD8B8F91044579FD9012F058789E1CB`。J-Link退出码0，CPU0 Bank0更新186368 bytes并校验，CPU1内容一致；控制台日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49ad_mppt_hold_floor_debounce_dualcore_flash_console.log`。刷写后COM4短查确认v49ad、18.5..26.0 V/360 W、READY/CTRL OFF/PWM0/GPT停止、路径全关、fault0，日志`2026-08-20_v49ad_postflash_queryonly.log`；COM24未访问。下一步仍在未改变的20 V/INC原点执行r104。

r104在未改变的输入20.0 V/限流5 A以上/CV33 V原点通过。下位机AI OFF、INC、LFP CC自主完成全模式比较，最终选择EPS37/0 ns、实际硬件量化36/0 ns、PinBest=101.41 W、etaBest=91.479%。选择后Vin曾跨过18.5 V边界且最后40点范围18.28~18.86 V，但没有再次进入stage1；最后40点Iin均值5.36 A、范围5.34~5.42 A，Pin均值99.85 W、范围97.73~101.58 W，Pout均值91.25 W、范围90.25~93.02 W，温度最高36.49/31.42 ℃，fault0。结论`PASS_NOMINAL_VIN20_INC_MPP_RETAIN_HOLD_FLOOR_DEBOUNCE_V49AD_R104`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM5PPLUS_CV33_v49ad_r104_inc_hold_floor_debounce_continuous.log`。结束只发送高层`bat stop`并释放COM4，COM24未访问。该点INC门禁关闭，同一物理工况下一步验证P&O。

r105保持同一20 V/限流5 A以上/CV33 V工况并改用P&O，下位机自主选择EPS29/0 ns、实际量化28/0 ns、PinBest=102.38 W、etaBest=91.530%；最后40点Vin=18.48~18.87 V、Iin均值5.35 A/范围5.28~5.38 A、Pin均值99.61 W/范围98.29~101.26 W、Pout均值90.89 W/范围89.64~91.72 W，温度最高38.86/32.75 ℃。选择后没有stage1重搜，fault0。结论`PASS_NOMINAL_VIN20_PO_MPP_RETAIN_HOLD_FLOOR_DEBOUNCE_V49AD_R105`；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_VIN20_ILIM5PPLUS_CV33_v49ad_r105_po_hold_floor_debounce_continuous.log`，结束只发送高层`bat stop`并释放COM4，COM24未访问。至此当前约100~124 W源限流层的20/22/24 V、INC/P&O均已通过。

操作者随后明确要求避免每个新工况重复两种MPPT算法。根据已完成的同工况对比，P&O在22 V与24 V最后40点平均Pin分别比INC高2.77 W和2.46 W，20 V仅低0.24 W；三点选择效率相当且固件冷启动默认已经是P&O。因此后续正式主线固定为`P&O + 本地SPS/EPS/TPS自主搜索`；新Vin/功率层只执行P&O。INC保留为本地可用备选，但仅在修改MPPT公共逻辑、代表性抽检或P&O出现异常时复测，不再机械双跑。该决策不改变“只训练调制选择与MPPT两个模型”以及AI OFF完整运行要求。

v49t已按新的下位机独立MPPT架构完成离线实现、严格编译、主机断言、双核构建、哈希和产物固化。新增自适应表项`33090001`不再使用静态电流分区决定模式，而是在CC中段先用SPS单外相移寻找输入源峰值；随后只在需要时依次开放EPS第二相移、TPS第三相移，并分别以16 ns步长扰动。每种模式的ADC `Pin`与`Pout/Pin`观察窗为10 s，EPS/TPS前各留3 s稳定；若增加自由度不能带来实质Pin收益，或在Pin相差不超过2%时效率未提高0.3个百分点，则自主回退到较低复杂度。TPS无收益回EPS、EPS无收益回SPS；Vin或负载显著变化后下位机自主重搜。最终上位机只需发送电池profile、P&O/INC及start/stop，AI OFF下全部逻辑仍由本地下位机运行。

大功率平滑规则已作为硬约束固化：电流参考升降均不超过0.5 A/s，inner相移变化不超过1 ns/ms，SPS/EPS/TPS切换保持PWM连续并同步补偿外相移，禁止用反复硬启动完成正常搜索。20~24 V为MPPT适用范围，360 W仅为电路绝对边界，不是点级目标或功率上限。相位搜索主机测试模拟SPS 50 W/90%、EPS 55.5 W/92%、TPS 54.5 W/91%，最终正确保持EPS；严格ARM C11、生成表`29/13/2`、promoted table、energy生产/测试编译和双核完整构建均通过。CPU0/CPU1 text/data/bss=`183722/560/172577`、`17682/132/9112`。产物目录`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49t_mppt_phase_search/`；CPU0 ELF/SREC SHA256=`0393BB69EF4A7BD731525572513A11AB343D154E01903476CF3D1184056EAF87`/`DFBFA79FF9377E9710BF4306C9BDEA510EEE4D90C0AC2ADDB5858557E4D5F17C`，CPU1=`779449F5EFB0F332DBBD5D01C9AEE7ACE11B64A16AF4F7F5AEBBA56DED260A9B`/`FCCB37805D6B35CD97CA43E22428CF4F869106A7D26884A81299CC23446176FA`。当前只记`OFFLINE_READY_NOT_PHYSICAL_PASS`；尚未刷写、尚未启动功率，也未访问COM24。

v49o已完成双核刷写。刷写后仅通过COM4短时查询并立即释放串口，确认版本`2026-08-20-dualcore-mppt-power-search-v49o`、READY、CTRL OFF、PWM0、GPT RUN0、PRE/MAIN/OUT/RESET_N/RUN_REQ全0、fault0；板端Vin/Vout约21.33/0.11 V。查询日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49o_postflash_queryonly.log`。这只证明升级和安全空闲，不是MPPT实物PASS；全过程没有访问COM24。

在准备22 V/2.5 A限流、CV33 V、`Pmax=60 W`实测前发现新的安全门禁：60 W输入需求在CV33输出折算为约1.8 A参考，会越过v49o的1.5 A调度边界并提前进入`entry=33050001`高电流SPS，而r83已证明该SPS在此电压比附近存在约184 W自然传输地板。因此未给v49o带功率，先形成v49p：LFP低压TPS720/180 ns分区延伸至请求2.0 A，高电流SPS仅在请求大于2.0 A后允许；MPPT仍最大化实测`Pin`，并未改成2 A或2.5 A恒流。参数生成检查`29/13/2`；参数边界测试、生产能量控制和MPPT测试均通过Renesas ARM C11 `-Wall -Wextra -Werror`编译。CPU0/CPU1完整构建text/data/bss=`180658/560/172405`、`17658/132/9112`，产物实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49p_mppt_lowknee/`。CPU0 ELF/SREC SHA256=`0D25FCD847503C1AD6E6F94A5F35A77D0BC5C92F5FB39A7B2F80DEAD32A4CC33`/`BED9B784BAC203350124010E7AFB15FEE44A237707D7C7D1CEE31A84AF1469CC`；CPU1=`25B5D97CCBE2DEED212F1D1B2EC62B092BE2A676F3827EF5EA5D07F1DF90A3CF`/`47769D0E314D7DC00BC506F6D41F5F8A69977E7EBA29FA015AE09E1FE9543FEF`。该2.0 A分区是为锁点实测采用的保守候选，尚未记物理PASS。

v49p已通过J-Link双核升级：目标`R7KA8P1KF_CPU1`，CPU0 Bank0完成182272 bytes编程与校验，CPU1内容一致，脚本退出码0；控制台日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49p_mppt_lowknee_dualcore_flash_console.log`。刷写后仅用COM4短时回查，确认版本`2026-08-20-dualcore-mppt-lowknee-v49p`、READY、CTRL OFF、PWM0、GPT RUN0、所有功率路径关闭、fault0，板端Vin/Vout约21.33/0.10 V；日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49p_postflash_queryonly.log`。COM4随后释放，电子负载未访问。下一锁定点为操作者手动设置输入22.0 V/限流2.5 A/OUTPUT ON并保持电子负载CV33.0 V/LOAD ON，完成后保留完整10 s人工稳定窗口，再仅通过COM4启动INC融合MPPT。

r86收到操作者完成确认后完整保留10 s无仪器访问窗口，再仅通过COM4启动v49p、AI OFF、LFP12、融合SOLAR_CHARGER、20~24 V、250 ms、`Pmax=60 W`和INC；采集无固定超时，电子负载全程未访问。下位机顺利进入CC并选择`entry=33022001` TPS720/180 ns，稳态板端约Vin21.09~21.11 V、Iin2.00~2.06 A、Pin42~43.5 W、Vout32.89~32.94 V、Iout1.05~1.08 A、最高温度约30 ℃、fault0，没有进入184 W SPS地板，证明v49p分区门禁有效。但INC在CC入口/执行束收敛瞬态中取得负搜索方向，进入刚性电压平台后`dV`落入死区、`dP`又不足1 W，旧逻辑持续保留负方向，使Ptarget从约36 W逐步减到约27~29 W，未向2.5 A限流折点爬升。结论`FAIL_FMP_LP55_INC_STALE_NEGATIVE_DIRECTION_V49P_R86`，锁定原点不测下一工况。检测后只发送一次高层`bat stop`；独立回查v49p为READY、PWM0、所有路径关闭、fault0。连续日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_VIN22_ILIM2P5_CV33_v49p_r86_inc_continuous.log`，停机回查为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_v49p_r86_postfail_queryonly.log`。

v49q针对r86原点修复：每次融合路径重新进入CC并初始化MPPT时，搜索方向明确重置为增载；INC在刚性平台`|dV|<=0.05 V`且`|dP|<=1 W`时把“无响应”解释为尚未找到拐点并重新向增载方向探测，只有真实Pin损失或可测源电压斜率才决定回撤。该规则仍优化实测Pin，不使用2.5 A恒流目标；Vin<20 V慢回撤、0.5 A/s物理电流斜率、连续PWM和v49p 2.0 A调制分区均保持不变。新增r86负方向复现断言；参数边界测试、生产算法和MPPT测试均通过Renesas ARM C11 `-Wall -Wextra -Werror`编译，参数生成检查`29/13/2`。CPU0/CPU1完整构建text/data/bss=`180666/560/172405`、`17682/132/9112`，产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49q_mppt_cc_reseed/`。CPU0 ELF/SREC SHA256=`67FD3888627573F7FDC417A431ECE6A0EE0D1EBE398F114F5E919F832EE2D144`/`3DFB9D94167DAF45221F1ABF69771F109FA01D5BE92DC3BBF8625AE91C5290AF`；CPU1=`779449F5EFB0F332DBBD5D01C9AEE7ACE11B64A16AF4F7F5AEBBA56DED260A9B`/`FCCB37805D6B35CD97CA43E22428CF4F869106A7D26884A81299CC23446176FA`。当前尚未刷写、尚未原点实物PASS。

v49q随后完成双核升级：J-Link连接`R7KA8P1KF_CPU1`，CPU0 Bank0 182272 bytes及CPU1 Bank0 18432 bytes均完成编程与校验，退出码0；控制台日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49q_mppt_cc_reseed_dualcore_flash_console.log`。刷写后仅用COM4短时查询并释放，确认版本`2026-08-20-dualcore-mppt-cc-reseed-v49q`、READY、CTRL OFF、PWM0、GPT RUN0、所有功率路径关闭、fault0，板端Vin约21.33 V；日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49q_postflash_queryonly.log`。电子负载未访问，22 V/2.5 A/CV33 V原工况未改变；下一步只复测r86，不进入新工况。

r87保持同一22 V/2.5 A限流、CV33 V原点，完整保留10 s无仪器访问后仅通过COM4启动v49q INC。方向修复生效：融合算法把Ptarget从约52 W连续爬升到60 W上限，电流参考命令由约1.58 A平滑增加到1.82~1.83 A，没有反向塌缩；但实际输入仍停在Vin约21.06 V、Iin约2.00~2.05 A、Pin约42~43 W，输出约Vout32.84~32.88 V、Iout1.06~1.08 A、fault0。执行束始终为已验证`entry=33022001` TPS720/180 ns，outer_saturated=1，命令相移20 ns、有效量化相移22 ns，证明瓶颈已从MPPT方向逻辑转移为TPS20 ns执行器饱和，尚未触及电源2.5 A限流折点。结论`FAIL_FMP_LP55_INC_TPS20_ACTUATOR_SATURATION_V49Q_R87`，继续锁定原点。检测后仅发送一次高层`bat stop`，停机回查READY、PWM0、所有路径关闭、fault0；连续日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_VIN22_ILIM2P5_CV33_v49q_r87_inc_continuous.log`，停机日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_v49q_r87_postfail_queryonly.log`。全过程未访问COM24。

v49r只对正常低压CC的TPS720/180 ns束做一个保守执行器步进：相移上限由20 ns增加到28 ns（两个硬件量化计数），用于验证能否把输入从约2.0 A继续平滑推向2.5 A折点；超低功率TPS832束和PRECHARGE仍保持20 ns，高功率SPS未启用，0.5 A/s物理电流斜率、连续PWM、60 W安全搜索上限和Vin<20 V回撤均不变。固件版本`2026-08-20-dualcore-mppt-tps28-v49r`、充电表revision17。参数边界测试、生产能量控制和MPPT测试均通过Renesas ARM C11 `-Wall -Wextra -Werror`编译，参数生成检查`29/13/2`；CPU0/CPU1完整构建text/data/bss=`180690/560/172405`、`17682/132/9112`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49r_mppt_tps28/`，CPU0 ELF/SREC SHA256=`3D91397D1E007E733D3F322C9376CEADD70630FE6FE8FA137AA07AF27B37CCC7`/`AE4941A833C49EFD63BDD09C026420BACC4D9AEE4F6A193FFED6FC61C28FCB80`，CPU1=`779449F5EFB0F332DBBD5D01C9AEE7ACE11B64A16AF4F7F5AEBBA56DED260A9B`/`FCCB37805D6B35CD97CA43E22428CF4F869106A7D26884A81299CC23446176FA`。J-Link随后以`R7KA8P1KF_CPU1`完成CPU0 Bank0 182272 bytes编程与校验，CPU1内容一致，脚本退出码0；控制台日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49r_mppt_tps28_dualcore_flash_console.log`。刷写后仅用COM4短时查询并立即释放，确认版本v49r、READY、CTRL OFF、PWM0、所有路径关闭、fault0，板端Vin约21.33 V；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49r_postflash_queryonly.log`。升级门禁通过但尚不是实物PASS；电子负载没有被访问。

r88在同一22 V/2.5 A限流、CV33 V原点完整保留10 s后，仅通过COM4启动v49r INC；无固定采集超时。Ptarget平滑爬升至59~60 W，电流参考约1.80~1.83 A，执行束始终`entry=33022001` TPS720/180 ns，命令/有效相移达到28/30 ns并保持outer_saturated=1。稳态板端约Vin20.97 V、Iin2.05~2.09 A、Pin43.0~43.9 W、Vout32.70~32.74 V、Iout1.09~1.11 A，最高温度约30.0/28.5 ℃、fault0；相较20 ns只小幅增加，仍未达到2.5 A折点。结论`FAIL_FMP_LP55_INC_TPS28_ACTUATOR_SATURATION_V49R_R88`，继续锁点。检测后只发送一次高层`bat stop`，停机连续遥测显示Vin恢复约21.34 V、Vout约0.10 V、运行状态READY、控制/PWM/功率路径关闭、fault0；连续日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_VIN22_ILIM2P5_CV33_v49r_r88_inc_continuous.log`，停机日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_v49r_r88_postfail_queryonly.log`。COM24未访问。

v49s沿用已有物理经验而不是继续盲目小步：2026-08-15同一TPS720/180形状曾在Vin约23 V、外相移80/82 ns下实测Pin82.36 W、Iin3.58 A、fault0，因此把正常低压CC束上限由28 ns放宽到该已知无故障边界80 ns；超低功率TPS832与PRECHARGE保持20 ns，高功率SPS仍隔离。版本`2026-08-20-dualcore-mppt-tps80-v49s`、表revision18。严格ARM C11编译和参数检查`29/13/2`通过，CPU0/CPU1完整构建text/data/bss=`180690/560/172405`、`17682/132/9112`；产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49s_mppt_tps80/`。CPU0 ELF/SREC SHA256=`7F83758934C5F334976B2B23293DD8CB0FBF92F6CFE3BF58B7F80BFAF38E68BE`/`D59304E7068C6189E0EEA3052200FA4BFB63A82572EE155733F1CF300551268A`，CPU1=`779449F5EFB0F332DBBD5D01C9AEE7ACE11B64A16AF4F7F5AEBBA56DED260A9B`/`FCCB37805D6B35CD97CA43E22428CF4F869106A7D26884A81299CC23446176FA`。J-Link完成CPU0 Bank0 182272 bytes编程与校验，CPU1内容一致，退出码0；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49s_mppt_tps80_dualcore_flash_console.log`。刷写后COM4短查确认版本v49s、READY、CTRL OFF、PWM0、所有路径关闭、fault0，Vin约21.34 V；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49s_postflash_queryonly.log`。升级门禁通过，尚未实物PASS；COM24未访问。

操作者纠正r86~r88仍使用`Pmax=60 W`会人为限制MPPT。该限制从下一轮起永久取消：产品和全部实测都只保留电路额定360 W绝对边界，不再按预计MPP给出点级功率上限。22 V/2.5 A工况的最大功率由输入源限流折点自然形成；下位机从低取能增载，持续比较实测Pin，越过折点后依据Vin下降和Pin损失自行回撤。2.5 A仍不是恒流目标，上位机也不提供功率目标。r86~r88日志中的60 W只保留为历史错误配置证据，不进入交付参数。

r89取消点级功率上限，仅保留360 W电路边界后，v49s固定TPS路径把请求从约50 W继续抬升，最终板端到Vin约19.6~19.9 V、Iin约2.84~2.89 A、Pin约55.6~57.4 W、Pout约50~51 W，证明输入源限流折点确实可被触及；但运行调度在请求跨过2 A分区时短暂切入`entry=33050001` SPS，又返回`entry=33022001` TPS，模式仍由静态电流分区决定，而不是比较SPS/EPS/TPS相移自由度后的MPPT结果。操作者据此纠正架构：DAB MPPT必须先搜索SPS单外相移，单自由度足够则保留SPS；不足再开放EPS第二相移；只有EPS仍不足才试TPS第三相移，TPS若没有提高最大Pin/效率则回EPS。结论`FAIL_FMP_LP55_FIXED_TPS_AND_STATIC_MODE_SCHEDULER_V49S_R89`。主机立即只发送一次高层`bat stop`，连续日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP55_VIN22_ILIM2P5_CV33_v49s_r89_inc_pmax360_continuous.log`；COM24未访问。

新的交付门禁：串口调试可临时修改搜索步长/驻留，但最终上位机只发送电池profile、P&O/INC和start/stop。SPS→EPS→TPS复杂度递进、每个相移扰动、ADC Pin与Pout/Pin评分、无净收益回退、连续PWM平滑切换、工况变化后重搜必须全部由下位机独立完成；AI OFF必须功能完整。固定TPS相移上限继续调大不再作为修复方向。

r84在操作者完成输入22.0 V/限流2.0 A、电子负载CV33 V/LOAD ON设置并保留完整10 s人工操作窗口后，仅通过COM4启动v49m融合MPPT；全程没有访问COM24。P&O阶段下位机自主选择`entry=33022001`、TPS720/180 ns、cap20 ns并保持连续PWM，稳态板端约Vin21.07 V、Iin2.00 A、Pin42 W、Vout32.86 V、Iout1.05~1.07 A、Pout34.5~35.3 W、fault0。说明“实时MPPT参考参与下位机选束”链路修复生效，但普通台式电源加限流折点不具备真实光伏P-V曲线，因此这里只记`PASS_FUNCTIONAL_CHAIN`，不记真实MPP精度PASS。

在同一连续会话切到增量电导后，ADC量化噪声使Vtarget逐步走到24 V，而刚性电源无法跟随该目标；输入电源进入2 A限流并使板端Vin降至约18.6 V，超出20~24 V范围。Ptarget降到约7~10 W、实时电流参考降至约0.2 A后，v49m调度器又从低功率TPS束逸出到未针对该点验证的生成表项`571992544`（约TPS440/208 ns、cap260 ns）。实际功率仍约Pin39 W/Pout35 W、Iout约1.06 A、最高温度约34 ℃、fault0，没有出现MOS过功率，但算法与选束均不满足要求，结论`FAIL_FMP_LP40_VIN22_INC_VIN18P6_AND_UNVALIDATED_ENTRY`。同一会话随即只发送一次高层`bat stop`，独立回查v49m为READY/PWM0/RUN0、四路路径关闭、fault0，Vin恢复约21.35 V。原点保持锁定，不测下一工况。原始日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP40_VIN22_CV33_v49m_r84_po_continuous.log`，停机回查为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP40_r84_postfail_queryonly.log`。

v49n针对同一失败点完成离线修复：INC的dV/dI噪声死区改为0.05 V/0.05 A；Vin低于配置下限20 V时立即把Vtarget指回20 V，并以0.5 W/10 ms撤回慢功率命令，实际电流仍由CPU0既有0.5 A/s双向斜率限制。LFP低压CC请求`<=0.8 A`只允许进入已在同板CV33物理筛过的TPS832/180 ns、cap20 ns新分区`entry=33022002`；`0.8~1.5 A`继续使用TPS720/180 ns `entry=33022001`，避免再选未验证生成表项。P&O反向判据同时提高到1 W实测功率变化，抑制噪声抖动。新增INC噪声/欠压恢复及0.80/0.81 A边界断言；Renesas LLVM不含WASM后端，主机WASM运行器不可用，因此不虚构断言执行PASS。参数生成检查`29/13/2`和生产ARM编译均通过，CPU0/CPU1 text/data/bss=`180682/560/172405`、`17710/132/9112`。产物已实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49n_mppt_vin_recovery/`，当前尚未刷写、尚未实物PASS。

刷写前仅用COM4查询v49m，板端Vin/Vout约21.33/0.10 V，state=READY、control=0、PWM=0、fault0。J-Link以`R7KA8P1KF_CPU1`连接并完成CPU0 Bank0 182272 bytes、CPU1 Bank0 18432 bytes编程校验，退出码0。刷写后只读回查版本`2026-08-20-dualcore-mppt-vin-recovery-v49n`，READY、CTRL OFF、PWM0、GPT RUN0、PRE/MAIN/OUT/RESET_N/RUN_REQ全0、fault0，AI deterministic fallback=LOCAL/OFF。CPU0 ELF/SREC SHA256=`6680A4C1FE1C5CDC436760C722A3AF662DC3E3585167450409A2043FF244D649`/`54DCA9481E5CD8D41C4B28E05745F08B54CE75F91BC1712A5F041C993CE92D2D`，CPU1=`342646814DA16EE609941701B98B72947CE6517CC26D449D49567C6946D6E24E`/`DF16B15ED174F9BB9AD03136EBDD0892E32733BA419825877A62120DE56335B5`。刷写和停机门禁通过不等于带功率PASS；下一步仍在同一22 V/2 A折点、CV33 V原点先复测INC。

r85在同一22 V/2 A限流折点、CV33 V/LOAD ON上复测INC。v49n成功消除了r84的Vin18.6 V越界和未验证表项跳转：运行Vin始终约20.94~20.99 V、fault0，调度只在已实测的`entry=33022001` TPS720/180与`entry=33022002` TPS832/180之间慢速过渡。但INC仍把不可达电压目标抬高，旧电压误差积分把Ptarget从约40 W持续降到0 W；物理TPS832自然传输地板仍有Pin约29 W、Pout约22 W、Iout约0.67 A。结论`FAIL_FMP_LP40_INC_POWER_TARGET_COLLAPSED_TO_ZERO_V49N`。检测后只发送一次高层`bat stop`，独立回查v49n为READY、control0、PWM0、fault0；原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP40_VIN22_CV33_v49n_r85_inc_continuous.log`，停机回查`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP40_v49n_r85_postfail_queryonly.log`。电子负载没有被访问或控制。

操作者随后纠正MPPT目标：若台式电源限流设为2.5 A，算法应主动寻找最大`Pin=Vin×Iin`并追到2.5 A折点附近，而不是恒定40 W。该纠正写入总计划v8：低功率门禁改为22 V/2.5 A形成约55 W伪PV折点，`Pmax=60 W`只作为略高于折点的安全搜索上限；结果以实测Pin峰值和折点附近回退/振荡判断，2.5 A不是串口恒流目标。`fixed`仍不进入产品路径。

v49o源码据此把P&O/INC从“电压目标误差长期积分为功率命令”改成“每250 ms直接扰动输入功率需求并比较实测Pin”。P&O仅在Pin下降超过1 W时反向；INC在dV足够时按`dI/dV+I/V`决定增减负载，在刚性电压平台dV近零时退化为实测功率扰动，持续爬升直到限流折点而不是停在常数功率。每次搜索步至少1 W；Vin<20 V仍慢命令回撤，CPU0物理电流双向0.5 A/s斜率、连续PWM和调制慢切机制不变。新增离线断言覆盖刚性源平台从约42 W持续爬升到至少55 W以及欠压回撤；测试和生产源均已通过Renesas ARM C11 `-Wall -Wextra -Werror`编译，CPU0/CPU1完整构建text/data/bss=`180682/560/172405`、`17658/132/9112`。当前仅完成源码和构建，尚未刷写、尚未实物PASS。

v49m低功率调度修复已固化并完成双核升级。参数生成检查为charger/current/voltage=`29/13/2`；CPU0 ARM构建text/data/bss=`180578/560/172405` bytes，CPU1=`17646/132/9112` bytes。产物目录`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49m_mppt_lowpower/`，CPU0 ELF/SREC SHA256=`872C1B9DCB8255FE2C2459516489AC26B36163C177BB60F576B04C1B5DBFAA7A`/`A392F3940D1BB8DDFD3857BF4DD18FB653B1A97BE0097FBE3195E80D03C774B9`。刷写前v49l只读回查确认Vout约0.1 V、PWM/RUN和四路路径关闭、fault0；J-Link连接`R7KA8P1KF_CPU1`后完成CPU0 Bank0 182272 bytes编程校验，CPU1内容一致，退出码0。刷写后只查询确认版本`2026-08-20-dualcore-mppt-lowpower-v49m`、READY/CTRL OFF/PWM0/RUN0、四路路径关闭、fault0，默认P&O/Pmax360 W、LFP Imax/Ipre/Iterm=`10/1/0.3 A`。以上只证明构建、升级和安全停机，不是MPPT实物PASS；电子负载未访问。

操作者最新要求把功率推进顺序改为：先完成小功率全部MPPT工况，再逐级升大功率。当前锁定40 W层，沿用板上已烧录的v49l和120 ns低压SPS相位上限；刚才根据r82准备的220 ns高功率表项只存在于未构建源码，不刷写、不启用。随后操作者明确固定功率不是项目运行功能，电子负载保持CV，当前需要真实MPPT。因此`fixed40`只保留已经取得的失败诊断/对照数据，不再对20/22/24 V和三调制重复；40 W层改为输入源形成约40 W可用功率，依次覆盖P&O/INC，并由下位机自主选模。调参阶段三模式各保持10 s只用于生成本地排名，最终PASS只下发profile、MPPT算法、Pmax和start/stop。失败仍锁点修复；40 W全层通过后才进入120 W，随后才是240/300/360 W。

为满足操作者调节工况期间不停止功率的硬要求，持续采集脚本增加可选的追加式串口命令队列和`EmergencyTimeoutSeconds=0`禁用固定超时；高频COM4遥测静默落盘，电子负载仍不访问。命令队列只用于在同一板端会话中追加MPPT算法和`modswitch`高层命令，停止仍由明确的工况完成信号触发。

r83在Vin设定22 V、电子负载CV33 V、fixed40 W、AI OFF下实测失败。CPU1/ALG持续给出MPPT Ptarget40 W、EnergyIref约1.20 A、CC阶段3，但CPU0调度仍按profile Imax10 A选择`entry=33050001` SPS0/0。末200个RUN样本平均Vin/Vout/Iout=`20.97 V/33.20 V/5.55 A`，Pin/Pout=`210.84/184.25 W`，原边温度均值41.74 ℃、末窗最高42.84 ℃，phase始终0、fault0；结论`FAIL_FMP_LP40_VIN22_FIXED_SPS_NATURAL_FLOOR_184W`。该SPS数据保留为低功率不适用负样本，不重复冲击。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_LP40_VIN22_CV33_v49l_r83_continuous.log`。

根因锁定为参数调度使用`battery_max_current_a=10 A`，忽略融合MPPT最新1.20 A实时参考。v49m修订为调度函数显式接收CPU1最新`energy_current_reference_a`：CV33且参考≤1.5 A时选已有实测TPS720/180低功率束，超过1.5 A才选SPS大电流束；无有效实时参考时才回退profile Imax。未验证的220 ns修改撤回，高电流SPS仍保持120 ns。该修复只改变下位机自主参数分区，不让上位机发送电流或底层调制参数。

## 先前检查点：融合MPPT加速矩阵锁定，等待FMP-MODE-20人工工况

按操作者最新硬要求，融合MPPT只覆盖输入20~24 V，最大功率必须覆盖电路额定360 W，不能把180 W当作最大功率点。执行矩阵更新为Vin按`24→22→20 V`顺序、Pfixed=`40/120/240/300/360 W`、每点SPS/EPS/TPS各10 s稳态，共45个调制子点；升降功率与模式渐变时间另计。高功率电流参考双向`≤0.50 A/s`，调制切换保持PWM连续、inner按`≤1.0 ns/ms`渐变、最短驻留3 s，禁止用反复关断重启切换。每个Vin/功率完成后立即保存三模式板端Pout/Pin相对效率、温升斜率、电流纹波、切换偏差、fault和best_mode；按操作者要求不单列示波器ZVS门禁，以板端ADC效率为主选择更有利于软开关的模式。任一子点失败仍锁在原Vin/功率修复。

固件v49k的两种锂电profile默认Imax=5 A，CV36 V时天然把融合MPPT限制在约180 W。当前修订把10 Ah默认锂电档改为1 C/10 A，并在下位机对CHARGER_ONLY与SOLAR_CHARGER统一执行`min(电池阶段允许电流, 360 W/Vout, MPPT功率/Vout)`；预充仍1 A、终止仍0.3 A，CPU0继续执行双向电流斜坡。该修改形成新固件前，必须重新关闭两种电池PRECHARGE→CC、CC→CV/DONE和Vin20/22/24固定CV回归，再做一次串口上位机联调；智能充电当前固件未完成门禁前禁止进入MPPT实测。

v49l离线版本已形成：`2026-08-20-dualcore-charge-360w-v49l`。固件同时把绝对功率上限统一为360 W，模式切换默认inner渐变收紧为1.0 ns/ms、最短驻留3 s、偏差超过20%平滑回退、超时2 s；大电流CC参考继续双向0.50 A/s。参数表检查29/13/2；MinGW四组C测试全部PASS，上位机Python3.13回归30/30 PASS；CPU0/CPU1 ARM构建text/data/bss=`180690/560/172405`与`17646/132/9112`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49l_charge_360w/`，CPU0 ELF/SREC哈希=`50FD2E5129118EBC6D094A879469017CF15D25792EF69DE316EB81ACE2F1A9F7`/`E682F86FF5199F7F2DCC4E78E4B3A27A0E18BD6ACE04B90041D5DB91B268B33E`，CPU1=`658DA236BDEF4ADBFFD266FB5988EC1BB2AE13BF42F88C3DA972D69E99E56FB4`/`0AC1F278AE24B628EAEBBEE512AF3CA7B34DDD8FD7BF01689342ABA92668C202`。该离线检查点尚无实物PASS，不能把构建结果写成智能充电已完成。

刷写前只通过COM4发送`version/show/bat show`查询，旧v49k为Vout约0.10 V、control/PWM关闭、fault0；电子负载未访问。J-Link v49l双核脚本退出码0，CPU0 Bank0完成182272 bytes、CPU1 Bank0完成18432 bytes的擦写/编程/校验。刷写后只读查询确认版本`2026-08-20-dualcore-charge-360w-v49l`、READY、CTRL OFF、PWM0、RUN_REQ0、PRE/MAIN/OUT/RESET_N全关、fault0，LFP默认Imax/Ipre/Iterm=`10.00/1.00/0.30 A`；模式切换回读slew1.00 ns/ms、pass5%、abort20%、timeout2000 ms、dwell3000 ms。原始日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49l_preflash_queryonly.log`、`2026-08-20_v49l_charge_360w_dualcore_flash.log`、`2026-08-20_v49l_charge_360w_dualcore_flash_console.log`和`2026-08-20_v49l_postflash_queryonly.log`。刷写与安全停机通过不等于带功率功能PASS；当前锁定智能充电门禁第一点LFP CV31预充。

电子负载继续由操作者独占，主机不访问COM24。当前板端v49k已停机；首点人工设置为输入20.0 V、电子负载CV36.0 V/LOAD ON，完成后保留完整10 s再从`FMP-MODE-20-P40`开始。

## 当前检查点：r80 v49k FMP-SAFE-02连续过渡PASS，转入融合MPPT

操作者在输入22 V、电子负载CV31 V/LOAD ON完成设置后，主机完整保留10 s无仪器访问窗口；随后同一COM4会话启动LFP12、AI OFF、`SOLAR_CHARGER`和fixed180 W。操作者把CV31→33 V期间功率传输始终保持，主机未访问COM24；操作者确认33 V完成后继续采集满10 s，才由同一会话发送高层`bat stop`。独立回查确认v49k停机、control0、PWM0、fault0。

2599组有效RUN DAB/ALG联合帧全部fault0。31.0/31.5/32.0 V附近Iout均值约0.999/1.011/1.026 A；整个31.0~32.4 V预充边界Iout为0.841~1.157 A，没有复现v49j的0.5 A下陷。首次CC在Vout32.394 V，Iout1.146 A、参考1.000 A；确认窗口后从TPS切换到专用SPS entry33050001，切换帧Iout由1.14 A到1.14 A。CC参考按0.50 A/s从1 A上升到5 A，任意相邻约50 ms帧Iout最大变化0.116 A、99百分位0.076 A，低于0.25 A/帧门槛；没有突然涌流。33 V末200帧Vout/Iout=`32.9981 V/5.5798 A`，Iout范围5.558~5.606 A，Pin/Pout=`212.08/184.13 W`，最高温度51.27/32.94 ℃。智能充电不要求高精度电流，5.58 A相对5 A请求属于趋势合理且比v49j的5.89 A错误自然下限改善；结论`PASS_FMP_SAFE_02_SMOOTH_PRECHARGE_TO_CC_TPS_TO_SPS_V49K`，允许进入FMP-MODE/FMP-MPPT。

上位机同步收敛：默认运行模式修正为有效键`智能充电 + MPPT`，窗口标题改为融合三功能；智能充电只显示电池类型、`MPPT算法（仅CC中段）`、固定功率基准和PV/充电功率上限，不显示/发送充电目标电压、电流或调制底层参数；独立MPPT不在界面中，配置对象明确标记`solar_with_charger=True`。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_SAFE_02_REDO2_LFP_CV31_TO33_VIN22_v49k_r80_transition.log`，停机回查`2026-08-20_FMP_SAFE_02_v49k_r80_postcheck.log`。

## 当前检查点：v49k已安全烧录，待FMP-SAFE-02原点复测

r79证明失败包含两个独立原因后，v49k仅在克隆固件内做针对性修复。LFP预充在31.0~32.4 V随实时Vout把已验证的TPS primary inner由832 ns平滑预交接到720 ns；Li-ion同步在29.0~30.0 V使用864→832 ns预交接。预充参考尚未到位时不施加局部电流纠偏，到位后采用r79实测确认的负控制方向。进入低压大电流CC时优先选专用SPS0/0桥接束，参数复用既有低压SPS闭环经验`Kp=9.785 ns/A`、`Ki=0.4892 us/(A*s)`，phase cap=120 ns，参考slew保持0.50 A/s；不再错误套用43 V/1 A的EPS自然传输下限束。所有状态判断、参数选择和电流闭环仍完全在下位机实时完成。

生成参数表检查charger/current/voltage=`29/13/2`；MinGW C11 `-Wall -Wextra -Werror`四组主机测试全部退出0；上位机自检`29/29 PASS`。CPU0/CPU1 ARM构建text/data/bss分别为`181002/560/172405`与`17654/132/9112` bytes。CPU0 ELF/SREC SHA256=`3B586F740D978459C184AC0C07AC5B9BD42BE9016E6FEB83E6D010603DF9D17F`/`FCEBFB370B14A435DC9815F8A392B2F7D4383F6431587DE7732C633E893B7252`；CPU1=`80F44BBE328B830BD6333D5D8A1FDF7DF3213B116AC756925A0951F962DF1A70`/`18FD428853711A7B31FDA052E6665FF5CF685BB707848B28E840392F003BB642`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49k_charge_transition/`。

烧录前只查询确认旧v49j处于停机遥测：Vout约0.10 V、输出电流约0 A、fault0。J-Link双核烧录退出码0，CPU0 Bank0完成182272 bytes编程与校验，CPU1内容匹配。烧录后只发送查询命令，确认版本`2026-08-20-dualcore-charge-transition-v49k`，READY、CTRL OFF、PWM0、RUN_REQ0、PRE/MAIN/OUT/RESET_N全关、fault0，温度约27.9/27.0 ℃，默认LFP Imax/Ipre/Iterm=`5.00/1.00/0.30 A`。COM4已释放，电子负载未访问；日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49k_charge_transition_dualcore_flash_console.log`、`2026-08-20_v49k_charge_transition_dualcore_flash.log`和`2026-08-20_v49k_postflash_queryonly.log`。v49k仍未形成实物PASS，下一步必须回到同一FMP-SAFE-02连续CV31→33 V点。

## 当前检查点：r79 v49j FMP-SAFE-02连续过渡FAIL，锁定原点修复

操作者把电子负载恢复为CV31.0 V/LOAD ON并确认完成后，主机完整留出10 s无仪器访问窗口；随后仅通过COM4启动融合`SOLAR_CHARGER`、LFP12、AI OFF和fixed180 W MPPT。持续采集由`Capture-DabSerialUntilSignal.ps1`保持同一个串口与功率会话，操作者把负载CV31→33 V期间没有发送停止命令，也没有访问、读取或控制COM24。操作者确认33 V完成后继续保持功率和板端遥测满10 s，之后同一会话才发送高层`bat stop`并释放COM4。独立查询确认版本v49j、READY、PWM0、RUN_REQ0、fault0。

有效匹配DAB/ALG共2359帧，其中RUN 2311帧、fault非零0、invalid0。PRECHARGE共548帧：Vout由约31 V升向32.4 V时，Iout从约1 A逐步降到0.646 A；在`32.418 V`首次进入CC，参考仍为1.000 A、实测0.687 A。日志进一步证明PRECHARGE TPS832/180 ns的外相移已饱和于20 ns，且正外相移从0升至20 ns时Iout反而由约0.90 A降到约0.78 A，属于该局部拓扑的错误控制方向/执行器边界，不是CPU1参考跳变。

CC阶段参考从1 A按0.50 A/s升到5 A，说明v49j的参考斜坡本身生效；但运行调度器随后应用仅在约43 V/1 A验证的`entry=416259073` EPS0/44 ns。CV33稳态最后200帧平均Vin/Vout/Iout=`20.860485 V/33.01383 V/5.893515 A`，Iout范围5.874~5.920 A，Pin/Pout=`222.6173/194.5672 W`，最高温度45.16/31.42 ℃，fault0。该束在33 V、零命令外相移时仍有22 ns正有效相移和约5.9 A自然传输下限，无法调回5 A目标。

结论`FAIL_FMP_SAFE_02_PRECHARGE_DROOP_AND_LOW_VOLTAGE_EPS_FLOOR_V49J`，禁止进入下一工况。修复继续复用已有实测经验：LFP在CV33的TPS720/180 ns曾稳定约1.08 A，低压SPS在CV36已有正方向电流闭环参数。下一版将在接近Vpre前把PRECHARGE primary inner由832平滑预交接到720 ns，预充局部采用正确的负方向且参考尚未到位时保持零控制动作；进入大电流低压CC后先使用SPS0/0低自然传输桥接束和0.50 A/s参考，再在更高电压进入正式效率选模。Li-ion同步加入更保守的864→832 ns预交接和同类低压SPS桥接。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_FMP_SAFE_02_REDO_LFP_CV31_TO33_VIN22_v49j_r79_transition.log`，后检`2026-08-20_FMP_SAFE_02_v49j_r79_postcheck.log`。

## 当前检查点：r76 v49j已安全烧录，FMP-SAFE-01 PRETEST

操作者返回并明确允许继续，随后主动断开上位机COM4。烧录前只发送`version/show/bat show`查询，确认旧v49i为READY、CTRL/PWM/RUN_REQ均关闭、PRE/MAIN/OUT/RESET_N全关、fault0，温度约25.9/25.7 ℃；未访问电子负载。v49j产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49j_charge_mppt_smooth/`，哈希与r75离线记录一致。

J-Link双核烧录退出码0：CPU0 Bank0完成182272 bytes编程校验，CPU1完成18432 bytes编程校验。烧录后只读回查确认版本`2026-08-20-dualcore-charge-mppt-smooth-v49j`，READY、PWM0、RUN_REQ0、四路功率路径全关、fault0，温度约26.1/25.8 ℃，profile默认Imax/Ipre/Iterm=`5.00/1.00/0.30 A`。COM4已释放；原始日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49j_charge_mppt_smooth_dualcore_flash.log`与`2026-08-20_v49j_postflash_queryonly.log`。

首个实物锁点`FMP-SAFE-01`：操作者手动设置输入电源22.0 V、电子负载CV31.0 V并保持LOAD ON。主机不访问、不读取、不写入电子负载。收到操作者“完成”后完整保留10 s无仪器访问，再只通过COM4下发AI OFF、LFP12 profile、`energy solar`、MPPT20.0~23.5 V/0.20 V/250 ms/Pmax180 W/fixed180 W及高层`bat start/stop`。预期电池阶段全程PRECHARGE，MPPT不初始化/不抬升电流，执行束TPS832/180 ns，Iout保持约1 A友好范围，fault0；失败则停留本点修复，不进入FMP-SAFE-02。

### r76 FMP-SAFE-01融合预充PASS

完整10 s人工稳定窗口后执行约10 s融合高层测试，电子负载从未被访问或控制。242个有效DAB帧中RUN 217帧、Vout30~32 V稳态201帧；末20点均值Vin=20.95105 V、Vout=30.71670 V、Iout=0.99360 A、Pin/Pout=37.2515/30.5210 W，Iout范围0.981~1.009 A，运行段峰值1.227 A，最高温度26.93/25.93 ℃，fault非零0、invalid0、entry始终30022001。230个活动ALG帧全部为SOLAR_CHARGER/PRECHARGE，能量参考上限1.000 A，MPPT fixed180 W没有覆盖充电状态机；遥测功率目标只报告当前预充允许值，最大为输出端瞬态约40 W而非180 W。

高层`bat stop`后独立查询确认v49j、READY、PWM0、RUN_REQ0、路径全关、fault0。结论`PASS_FMP_SAFE_01_LFP_PRECHARGE_MPPT_HELD_0P994A_V49J`，TPS832/180 ns预充束在融合功能下继续有效。

下一锁点`FMP-SAFE-02`必须在连续运行中记录操作者把电子负载CV31→33 V的瞬间，因此只持续读取板端ADC而不连接电子负载。预期CPU1按最新Vout进入CC，电流参考由1 A按不高于0.50 A/s上升；短时Iout变化、超调、模式请求/生效和参数束过渡必须同时满足v6平滑门槛，否则原点修复。

第一次FMP-SAFE-02采集使用35 s固定结束，操作者开始调整时脚本已经到达截止时间并发送高层`bat stop`；输出端只到约31.6 V，电池阶段仍为PRECHARGE，没有形成31→33 V完整过渡。该轮结论`ABORTED_HOST_TIMEOUT_DURING_OPERATOR_ADJUSTMENT`，不用于算法PASS/FAIL或训练数据。按操作者硬要求改为`Capture-DabSerialUntilSignal.ps1`：同一COM4会话持续记录且保持功率，只有操作者确认调到33 V并额外保持满10 s后才由主机创建停止信号并发送`bat stop`；另保留180 s异常兜底以避免无人值守持续上电。

## 当前检查点：r75 v49j融合智能充电/MPPT与平滑参考离线就绪，禁止启动

操作者决定把MPPT并入智能充电的中段大功率CC阶段，并把效率作为首要目标；PRECHARGE、CV、DONE仍必须由电池状态机优先控制。PRECHARGE→CC电流参考要求以不高于0.50 A/s爬升，调制切换同时验证电流无短时大幅变化。操作者随后明确离开一段时间并要求“不要启动”。从该指令起，主机没有连接COM4、没有读取或控制电子负载、没有烧录、没有启动任何功率，只进行以下离线工作。

测试总计划更新到v6并建立FMP-SAFE、FMP-MODE、FMP-MPPT、FMP-PV、FMP-AUTO-VIN、FMP-CV和FMP-AI-OFF逐点10 s工况。当前硬门禁为先通过`FMP-SAFE-01`，再在人工CV31→33 V变化中通过`FMP-SAFE-02`；若任一点失败，必须原点修复，不得进入模式效率与P&O/增量电导测试。真实P&O/增量电导PASS必须使用可复现P-V曲线和GT扫描，刚性稳压输入只能验证协调功能链。

固件源版本更新为`2026-08-20-dualcore-charge-mppt-smooth-v49j`。`SOLAR_CHARGER`现先执行电池状态机，仅在CONSTANT_CURRENT阶段运行MPPT；PRECHARGE/CV/DONE暂停MPPT并保持充电参考所有权。`bat start`在已经配置`energy solar`时保留融合模式；旧的独立充电调用仍向后兼容。大电流CC参考slew由v49i的1.0 A/s降为0.50 A/s，预充和CV的低速约束不变。AI OFF完整本地路径保留，AI只预留“模式选择”和“MPPT”两个可选接口。

上位机收敛为三个入口：`智能充电 + MPPT`、`CP能源路由`、`精准电流`，不再暴露独立MPPT。融合入口只发送`ai off`、明确电池profile、`energy solar`、20.0~23.5 V范围、step0.20 V、interval250 ms、fixed/P&O/INC及Pmax，随后由`bat start/stop`高层控制；不发送充电电压/电流、mod、inner、cap或PI。连接后自动创建JSONL，且未选择电池类型时禁止启动。上位机自检`29/29 PASS`。

离线验证结果：生成参数表一致性charger/current/voltage=`29/13/2`；MinGW C11 `-Wall -Wextra -Werror`已生成的四组当前主机测试程序逐一运行退出码0，其中energy测试明确输出`all host tests passed`；CPU0/CPU1 ARM构建均无错误，text/data/bss分别为`180538/560/172405`与`17654/132/9112`。CPU0 ELF/SREC SHA256=`8FE4631A71C17167495C4E104010DEF7878528D3D81DFEAD47175A0C2334F3BD`/`789A112BDB0FDCE19EBAF4BABBD4B51AA4EE06A3F590BE830A72F1D4D631BD77`；CPU1=`80F44BBE328B830BD6333D5D8A1FDF7DF3213B116AC756925A0951F962DF1A70`/`18FD428853711A7B31FDA052E6665FF5CF685BB707848B28E840392F003BB642`。

v49j当前仅是“离线就绪”，尚未烧录、尚未进行任何实物测试，不能标记为功能PASS。等待操作者返回并明确允许继续后，才可先做停机状态确认和烧录，再由操作者手动设置FMP-SAFE-01工况；没有新的明确授权前保持不连接、不启动。

## 当前检查点：r74 上位机44→31 V失败、隐式电池类型与日志缺陷修复

操作者使用明确路径`C:\Users\LENOVO\Desktop\Electric Simulation\RA8P1_DAB_UpperComputer\run_demo.bat`启动当前上位机，并报告在电子负载CV由44 V降至31 V时电流持续增大，结论立即锁定为`FAIL_CURRENT_MONOTONIC_INCREASE_NO_JSONL_PROFILE_UNCONFIRMED`，不进入下一点。主机没有访问或控制电子负载。进程核对确认实际执行该目录中的`python app.py`，不是另一份旧上位机。

本次没有生成JSONL。代码定位到“记录本机会话JSONL”默认关闭，导致无法事后确定操作者实际选中的profile、CPU1充电阶段、执行调制和电流序列；这是上位机缺陷。现已改为新会话默认自动记录，仍允许操作者明确关闭。静态和实际Python导入路径核对确认当前智能充电配置命令严格为`ai off`、`bat profile liion10|lfp12`、`alglog on`、`mode save`、`show`，启动只发送`bat start`；不存在`bat imax`、`bat vcv`、`mod`、`inner`或`cap`覆盖。上位机28项自检通过。

同时发现新上位机此前默认选中10S锂离子。板端锂离子Vpre=30.0 V，LFP Vpre=32.4 V；若操作者希望复现LFP的31 V预充但未重新选择电池，系统会在31 V仍按Li-ion CC运行，从高压CV向5 A CC恢复时表现为电流增大。由于缺少本次日志，这一解释仍是高概率根因而不是已证实结论。界面已取消隐式默认电池类型，智能充电每个新会话必须明确选择锂离子或磷酸铁锂，否则禁止应用/启动。下一锁点为明确LFP12、CV31 V直接启动并自动保存完整JSONL；若仍不进入PRECHARGE，同点继续修复。

## 当前检查点：r72 v49h安全刷写确认、v49i十秒CC响应修复完成构建

上位机释放COM4后，先以只查询方式确认旧v49g处于READY、control/PWM/RUN均关闭、四路功率路径关闭且fault0。随后J-Link将v49h写入CPU0 Bank0并完成182272 bytes校验，CPU1内容匹配；刷写后通过COM4只发送`version`、`show`、`bat show`，确认版本v49h、READY、PWM0、RUN_REQ0、PRE/MAIN/OUT/RESET全关、fault0，且电池profile默认值为Imax5 A、Ipre1 A、Iterm0.3 A。全过程未访问、读取、写入或切换电子负载。v49h没有进行带电功率测试。

带电测试前复核发现，v49h继承的CC记录电流变化率为0.05 A/s，从1 A恢复到profile默认5 A约需80 s，与每点10 s硬要求不符，因此没有浪费一次人工工况。v49i仅对`stage=CC`且请求电流大于1.2 A的正常大电流充电把运行时slew提高为1.0 A/s；PRECHARGE、CV及1 A工程CC记录继续保持0.05 A/s。没有增加Iin/Iout阈值停机，v49h的实时阶段严格选表和PRECHARGE断电重装自主重启逻辑全部保留。

参数生成器检查charger/current/voltage=`29/13/2`；MinGW C11 `-Wall -Wextra -Werror`通过promoted-table、energy state-machine、advanced-control和CAN protocol四组测试，并新增验证PRECHARGE5 A请求仍为0.05 A/s、普通CC5 A为1.0 A/s、CC1 A及CV5 A仍为0.05 A/s。上位机Python3.13自检`28/28 PASS`。CPU0 ARM构建text/data/bss=`180474/560/172405`，ELF/SREC SHA256=`B227A8F0EE38DAA6AFB98FDFAE0E4D04E88FA41E467A39922BB838C406BC369C`/`66A24DBBBC7E51533DEBD98050D9CC726DD472DA958FA88BCE9403A06DABE34A`；CPU1为`17558/132/9112`，哈希仍为`0A8E91A49CA7BCA1FD7DD946CBFCE5A115803252132A2843EE145645502BA104`/`DBCFB2A82ACBF991FD42024B3D523A8FBD84E65D7874AA3D5FB74022B588A714`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49i_charge_fastcc`。

刷写前只查询确认v49h处于READY、PWM0、RUN_REQ0、四路路径全关、fault0。J-Link随后完成v49i CPU0 Bank0共182272 bytes的擦写、编程与校验，CPU1内容匹配；刷写后仍只发送`version`、`show`、`bat show`，确认版本`2026-08-20-dualcore-charge-fastcc-v49i`、READY、control/PWM/RUN_REQ均关闭、四路路径全关、fault0，profile默认Imax/Ipre/Iterm=`5.00/1.00/0.30 A`。刷写前后Vin约21.41~21.42 V。电子负载未被访问或控制。v49i已安全就绪，但尚未进行带载智能充电验收。

## 强制记录流程

每个测试点上电前先写 PRETEST 记录并创建原始日志路径；运行后立即追加 RESULT 记录和本工作记录。所有失败、中止、误配置、未收敛与边界点保留。每次进入新工况前先阅读历史 WORKLOG、逐点 CSV 和原始日志，优先复用同工况 PASS 参数。

每完成一个功能后立即执行：PASS 参数固化到本地确定性固件表 -> CPU0/CPU1 构建与哈希 -> 固件升级 -> 启动确认 -> 串口上位机完整回归 -> 代表点复测。该闭环完成前不得进入下一功能。

## 已回填测试记录

### CV29 直接启动点：拒绝复用

- `LI_CV29_EPS_AUTO_01`：Vin 设定 24 V、RK8510 CV29 V、Li-ion、EPS AUTO、Imax/Ipre=1/1 A、Kp/Ki=2/0.2、slew=1 A/s、cap=20 ns。外部峰值 3.621 A/105.009 W，未进入 1 A 带；结论 `REJECTED_WORKPOINT`。
- `LI_CV29_TPS_AUTO_01`：同工况 TPS AUTO，外部峰值 3.867 A/112.131 W，未进入 1 A 带；结论 `REJECTED_WORKPOINT`。
- 这两个点仅证明当前直接启动组合不合适，不能作为 EPS/TPS 功能失败结论，也不得作为后续调参种子。此前 SPS 冒烟还包含错误的低主机停止阈值和配置确认失败，只作为 incident 保存，不复用。

### CV43 电流基线

- `CV43_TPS_MANUAL100_564_01`：TPS 100/564 ns 产生 3.657 A/157.196 W 峰值，明显偏离 1 A；结论 `REJECTED_WORKPOINT`，不复用。
- `CV43_EPS_MANUAL240_0_01`：EPS 240/0 ns、Kp/Ki=2/0.2、slew=0.05 A/s、cap=120 ns。12 s 运行仍在收敛，末段外部约 0.71~0.73 A；结论 `SETTLING_ONLY`。
- `CV43_EPS_MANUAL240_0_02`：从历史 `G0_EPS_CV43_I1` 原样复用并延长至 25 s。末三次外部读数 42.983 V、1.011~1.026 A、43.455~44.100 W；末段板端约 42.59~42.62 V、0.98~1.00 A、fault=0，温度约 30.3/29.1 ℃。结束 READY/PWM=0/RUN=0。结论 `PASS_BASELINE`，允许作为相同工况种子。

### Li-ion CV42.2 状态机

- `LI_CV42P2_EPS_AUTO_01`：复用 2026-08-16 Li-ion 历史配置，EPS AUTO 在 15 s 内完成 PRECHARGE→CC→CV，但 inner 仍在移动；外部峰值 3.382 A/142.652 W，末段没有稳态。结论 `PASS_STAGE_CHAIN_NOT_STEADY`，AUTO 扫描结果不作为参数种子。
- 从历史成功原始日志解码得到 AUTO 稳态落点约 primary/secondary inner=376~380/0 ns，因此下一轮只改变 inner 策略，改为手动 380/0 ns。
- `LI_CV42P2_EPS_MAN380_01`：PRECHARGE→CC→CV 顺序正确；外部峰值降至 2.344 A/98.874 W。14~20 s 外部稳态 42.180 V、1.122~1.142 A、均值 1.132 A/47.741 W；板端末段约 Vin=23.118 V、Vout=42.088 V、Iout=1.105 A、Pin/Pout=52.6/46.4 W，温度约 29.6/28.5 ℃，fault=0。CV 阶段 Icmd 约 1 A，属于接近 42 V 后的正常降流，不标为 1.5 A 恒流 PASS。结束 READY/PWM=0/RUN=0。结论 `PASS_STAGE_CHAIN_CV_TAPER`。

## 当前测试点：LF-CC-V24-01

目的：换管后复测 LFP12、Vin24 V、外部 CV42 V、CC 1 A 的历史成功点。

历史种子：`../2026-08-16_RK8510_v39/WORKLOG.md` 中 `passed_selected_42v`，数据行位于 `../2026-08-16_RK8510_v39/smart_charge_trajectory_points_24v_cv.csv` 的 `lfp12_v43_cv42_selected_eps`。

计划参数：EPS 240/0 ns，Imax=1.0 A，Ipre=1.0 A，Iterm=0.5 A，Kp/Ki=2.0/0.2，slew=0.05 A/s，cap=80 ns，power limit=100 W，Vref=44 V，AI OFF。与历史 PASS 相比不改变控制变量，只验证新 MOS 和 v47l 固件回归。

人工工况：输入电源 24.0 V/5.0 A/ON；RK8510 CV42.0 V、OCP3.0 A、OPP120 W、LOAD ON。主机只读确认后才运行。

### LF-CC-V24-01 结果

只读确认 RK8510 为 CV42.0 V/LOAD ON。状态机从 PRECHARGE 进入 CC，requested/effective=EPS/EPS，240/0 ns，AI OFF，fault=0。最后六个外部样本为 41.9963 V、1.228~1.231 A、均值 1.2290 A/51.613 W；板端末 20 个窗口均值 Vin=23.0625 V、Vout=41.8155 V、Iout=1.2040 A、Pin/Pout=56.350/50.375 W、Tpri/Tsec=29.09/27.93 ℃。外部峰值 1.231 A/51.698 W，结束 READY/PWM=0/RUN=0。

结论：`FAIL_ACTUATOR_FLOOR`。相移显示 raw=120 ns、effective=0 ns，cap=80 ns 持续饱和；新 MOS/v47l 下 240/0 ns 的最小传输电流约 1.23 A，超过本阶段 ±15% 带。历史 PASS 参数保留为证据，但不能直接固化到本轮固件。

下一步 `LF_CC_V24_02`：保持所有工况、充电参数、Kp/Ki、slew、cap 和 power limit 不变，仅把 primary inner 从 240 ns 增至 260 ns。历史相邻数据表明增大 primary inner 会降低零有效相移附近的电流下限；使用 20 ns 小步验证方向，不使用 AUTO 扫描。

### LF-CC-V24-02 结果

primary inner=260 ns 后，最后六个外部样本为 41.9927 V、1.255~1.257 A、均值 1.2560 A/52.743 W；板端末 20 个窗口均值 Vin=22.9445 V、Vout=41.6340 V、Iout=1.2345 A、Pin/Pout=57.295/51.395 W、Tpri/Tsec=29.84/29.10 ℃。外部峰值升至 2.463 A/105.808 W，fault=0，结束 READY/PWM=0/RUN=0。

结论：`FAIL_WRONG_DIRECTION`。相对 240 ns 基线，稳态电流由 1.229 A 升至 1.256 A，启动峰值也明显升高；禁止继续增大 primary inner。

下一步 `LF_CC_V24_03`：回到 240/0 ns 基线，仅把 phase cap 从 80 ns 改为 60 ns。历史记录 `lfp12_v43_cv42_cap60_inner240` 在同类工况外部约 0.95 A，属于比 inner 扫描更接近的相邻实测种子。Kp/Ki 仍保持 2.0/0.2，因此本轮只验证 cap 变化。

### LF-CC-V24-03 结果

phase cap=60 ns 后，最后六个外部样本为 41.9933 V、1.229~1.231 A、均值 1.2305 A/51.672 W；板端末 20 个窗口均值 Vin=23.0660 V、Vout=41.8185 V、Iout=1.2195 A、Pin/Pout=57.085/50.960 W、Tpri/Tsec=30.275/28.775 ℃。外部峰值 2.434 A/102.215 W，requested/effective=EPS/EPS，AI OFF，fault=0，结束 READY/PWM=0/RUN=0。

结论：`FAIL_CAP_NO_EFFECT`。在 raw=120 ns、effective phase=0 ns 的执行器下限状态中，cap 从 80 ns 降到 60 ns 没有改变约 1.23 A 的稳态电流下限。不得继续用 phase cap 盲调，也不得把本点固化为 PASS 参数。下一点先复核历史二级 inner、SPS 与 TPS 的同工况原始数据，再选已有 PASS 或相邻参数；选定前不启动硬件。

下一步 `LF_CC_V24_04`：历史 LFP/CV42 只有 EPS 240/0 ns 系列，没有可直接复用的二级 inner、SPS 或 TPS 低电流 PASS。改用今天同一块板、同一 v47l、相邻 CV42.2 工况已经实测的 `LI_CV42P2_EPS_MAN380_01` 作为新种子：380/0 ns 时外部稳态约 1.132 A，启动峰值 2.344 A。恢复 cap=80 ns，保持 LFP、CV42、1 A、Kp/Ki=2.0/0.2、slew=0.05 A/s、100 W、AI OFF 不变，仅验证 primary inner=380 ns；电子负载只读。

### LF-CC-V24-04 结果

380/0 ns 在 LFP CC 下最后六个外部样本为 41.9943 V、1.355~1.356 A、均值 1.3552 A/56.909 W；板端末 20 个窗口均值 Vin=22.9390 V、Vout=41.6420 V、Iout=1.3325 A、Pin/Pout=62.060/55.495 W、Tpri/Tsec=30.155/29.040 ℃。外部峰值 2.453 A/103.013 W，fault=0，结束 READY/PWM=0/RUN=0。

结论：`FAIL_HIGHER_FLOOR`。Li-ion 的 380/0 ns 结果属于 CV 阶段降流，不能迁移为 LFP CC 参数；在 LFP CC 下相移执行点 raw/effective 约 192/2 ns，稳态电流反而升高。本点禁止复用。

下一步 `LF_CC_V24_05`：保持 LFP/CV42/1 A、Kp/Ki=2.0/0.2、slew=0.05 A/s、cap=80 ns、100 W、AI OFF，只把调制改为 SPS 并将 inner 清零。SPS 去掉 EPS 内移相造成的零有效相移电流下限；起点沿用历史 `G2_SPS_CV36_I2` 的已闭环结构，并由 v47l 的 4 ns 起步和相位渐升完成低应力启动。

### LF-CC-V24-05 结果

SPS 在相位已经降到 raw/effective=0/0 ns 后，最后六个外部样本仍为 41.9943 V、2.501~2.502 A、均值 2.5012 A/105.034 W；板端末 20 个窗口均值 Vin=23.0985 V、Vout=41.9935 V、Iout=2.4880 A、Pin/Pout=114.965/104.465 W、Tpri/Tsec=31.165/29.910 ℃。外部峰值 2.502 A/105.068 W，fault=0，结束 READY/PWM=0/RUN=0。

结论：`FAIL_SPS_ZERO_PHASE_FLOOR`。SPS 在该 24 V→42 V、1 A 低功率点的零相移传输下限约 2.50 A，明显高于 EPS 240/0 ns 的约 1.23 A；SPS 不参与该工况的最终选择，也不继续调 PI。智能充电阶段不把 CV42 的精度问题混入后续精准恒流阶段：保留 EPS 为该点最低电流模式，并把 LFP CC 稳态/宽输入验证移到今天已经通过 1 A 闭环的相邻 CV43 工况。

## 当前测试点：LF-CC-V24-06

用户已完成人工切换：输入电源保持 24.0 V/5.0 A/ON；RK8510 设置 CV43.0 V、OCP3.0 A、OPP120 W、LOAD ON。测试复用本轮 `CV43_EPS_MANUAL240_0_02` 的 PASS 参数：EPS 240/0 ns、Imax/Ipre=1.0/1.0 A、Iterm=0.5 A、Kp/Ki=2.0/0.2、slew=0.05 A/s、cap=120 ns、power limit=100 W、Vref=44 V、AI OFF。电子负载仅以不高于 1 Hz 的频率短暂只读查询。

### LF-CC-V24-06 结果

状态机完成 PRECHARGE→CC 并保持 CC，requested/effective=EPS/EPS。参考斜率到达 1 A 后，最后三个外部样本为 42.984 V、0.975~1.018 A、均值 1.0007 A/43.012 W；板端末 10 个 RUN 窗口均值 Vin=23.088 V、Vout=42.806 V、Iout=0.953 A、Pin/Pout=46.670/40.870 W。外部全程峰值 1.018 A/43.759 W，温度末段约 30.93/29.77 ℃，fault=0，结束 READY/PWM=0/RUN=0。

结论：`PASS_LFP_CC`。EPS 240/0 ns、cap120 ns 在 CV43 下既保留相位调节余量，又准确跟随 1 A；允许作为 LFP CC 和后续宽输入的 Vin24 参数种子。下一工况切换 RK8510 到 CV44.0 V，保持其他设置不变，验证 CC→CV→降流→DONE。

## 当前测试点：LF-CV-DONE-V24-01

用户已完成人工切换：输入电源保持 24.0 V/5.0 A/ON；RK8510 设置 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON。板端参数保持 EPS 240/0 ns、Imax/Ipre=1.0/1.0 A、Iterm=0.5 A、Kp/Ki=2.0/0.2、slew=0.05 A/s、cap=120 ns、100 W、Vref44 V、AI OFF。计划记录 50 s，以覆盖低于 Iterm 后的 30 s 终止计时；电子负载只读。

### LF-CV-DONE-V24-01 结果

状态机完成 PRECHARGE→CC→CV。外部电流在 elapsed 6.855~21.275 s 约为 0.410~0.465 A，符合初始 CV 降流，但低于 0.5 A 的连续时间仅约 15 s；之后电流逐步回升，最后六点为 0.873~0.938 A、均值 0.9007 A/39.621 W。板端末 20 个窗口均值 Vin=23.067 V、Vout=43.7465 V、Iout=0.844 A、Pin/Pout=42.600/36.915 W、Tpri/Tsec=31.385/30.500 ℃。fault=0，结束 READY/PWM=0/RUN=0。

结论：`FAIL_CV_TAPER_REBOUND_NO_DONE`。CV 阶段本身成立，但板端 Vout 约 43.75 V，低于 LFP Vcv=43.80 V，电压环积分使电流重新上升并重置 30 s 终止计时，因此未进入 DONE。

### 失败工况闭环门禁与当前锁定状态

用户新增硬要求：任何工况 FAIL/未通过后，必须先修复并在完全相同工况复测到 PASS，不得继续相邻点或下一功能。此前提出把 RK8510 从 CV44.0 V 改为 CV44.1 V 属于绕过失败工况，现正式撤回，不执行且不得作为 PASS 替代。

当前唯一允许的后续动作是：电子负载保持 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON，输入保持 24.0 V/5.0 A/ON；先修复 CV 阶段积分导致的降流后回升机制，完成单元测试、CPU0/CPU1 构建、哈希和固件升级记录，再以 LFP12、EPS 240/0 ns、Imax/Ipre=1.0/1.0 A、Iterm=0.5 A、Kp/Ki=2.0/0.2、slew=0.05 A/s、cap120 ns、100 W、Vref44 V、AI OFF 原样复测。只有该点达到 CC→CV→连续低于终止电流→DONE 并完整落盘，才允许进入下一个工况。

### LF-CV-DONE-V24-01 修复轮次 1：软件根因与单变量修改计划

原始日志显示进入 CV 后板端 `Vout` 长期约 43.73~43.76 V，目标 43.80 V；外部电流已降到 0.410~0.465 A、板端电流约 0.42 A，满足终止电流候选，但当前 PI 仍以固定 0.1 s 步长累加正误差，`Icpu1` 随后从约 0.42 A 上升到约 0.98 A，直接重置终止计时。根因定位为 CV 终止候选期间缺少积分保持和请求电流上限，不修改电子负载工况，也不修改 LFP Vcv。

本轮只修改充电状态机的 CV 终止候选机制：仅当输出电压处于 CV 目标附近且实测电流不高于 Iterm 时，禁止正向积分并把本地请求限制在 Iterm；负向积分仍允许降流。若输入扰动使输出电压明显跌离 CV 目标，则立即退出候选、恢复正常 PI，以保证后续 Vin=18~24 V 变化时可以恢复电压。先加入“近 CV 低电流 30 s 不回升并进入 DONE”和“明显欠压时不误终止且允许恢复电流”两类主机测试，再构建固件。

### LF-CV-DONE-V24-01 修复轮次 1：测试与双核构建记录

- 算法版本：`2026-08-19-v44-cv-termination-hold`；仅修改 `shared/dab_energy_control.c/.h` 和对应主机测试。
- Windows 主机测试使用 GCC 13.1.0、`-std=c11 -Wall -Wextra -Werror -pedantic -O2`，输出 `dab_energy_control: all host tests passed`，退出码 0。新增测试同时覆盖原 43.75 V/0.42 A 终止候选 30 s 不回升并进入 DONE，以及 43.0 V 明显欠压时退出候选并恢复大于 Iterm 的电流请求。
- CPU0 增量构建通过，text/data/bss=`173726/560/172385` bytes；ELF SHA256=`666B3D05FE1F2CB37C96852A5D6CB833328B132A7285C9C697C225B265CFA031`，SREC SHA256=`098B37765A6DEA7F1B69C51AB6E69957D7DDECE6DB455700706562CF78CD2756`。
- CPU1 增量构建通过，text/data/bss=`17074/132/9104` bytes；ELF SHA256=`49801BB990B71DF5306B09ACA1DF6298AB800EF3E0C238EF63186365EF640923`，SREC SHA256=`E25211DE6AC1ADC5384E7D0348D729C6004BC107F9D325413C0816289AE95FF1`。
- `Verify-ReferenceParity.ps1 -RequireBuildArtifacts -AllowEthernetPhase1 -AllowEnergyControlV39` 对构建产物、内存分区、双核依赖和 Helium 指令等关键项通过，但当前 v47l 工作树仍有 9 项相对旧参考工程的既有差异（CAN/FSP、时钟、IPC 优先级及旧 v39 标识等）。这些不属于本次 CV 修复，不在本轮擅自修改；完整失败输出已保留在当前任务记录，不能把等价检查写成全通过。
- 烧录前串口确认：`READY`、`CTRL=OFF`、`PWM=0`、`RUNstate=READY`、四路功率路径均关闭、`fault=00000000`。输入板端约 23.32 V，电子负载不写入且保持人工设定。
- 计划烧录 CPU0 和 CPU1 当前 ELF，原始 J-Link 日志：`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_v47l_v44_cv_hold_dualcore_flash.log`。烧录完成后必须先确认启动 READY/PWM0/RUN0/fault0，再写复测 PRETEST；烧录成功不等于原失败工况已通过。

### LF-CV-DONE-V24-01 修复轮次 1：固件升级与原工况复测 PRETEST

J-Link 已连接 `R7KA8P1KF_CPU1`：CPU0 flash 内容比较一致而跳过写入；CPU1 有 18432 bytes 受影响，Erase/Program/Verify 均完成，J-Link 退出码 0。升级后串口已确认应用重新启动到 `READY/CTRL=OFF/PWM=0/RUN=0/fault=0`，四路功率路径均关闭；算法功能尚待同工况物理复测证明。

复测 ID：`LF_CV_DONE_V24_01_FIX1`。人工工况不变：输入 24.0 V/5.0 A/ON，RK8510 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON。板端配置与失败点完全相同：LFP12、EPS 240/0 ns、Imax/Ipre=1.0/1.0 A、Iterm=0.5 A、Kp/Ki=2.0/0.2、slew=0.05 A/s、cap120 ns、100 W、Vref44 V、AI OFF；唯一变化是固件中的 CV 终止候选积分保持机制。计划采集 50 s，RK8510 仅不高于 1 Hz 查询并立即释放 COM24；若仍未进入 DONE，则继续修复本点，不进入其他工况。

### LF-CV-DONE-V24-01 修复轮次 1：实板结果

状态机完成 PRECHARGE→CC→CV，但 DONE 计数为 0。原先的持续积分回升已经消失：末段 `Icpu1` 主要保持 0.50 A，不再缓慢升至 0.98 A；然而板端 Iout 在 0.49~0.51 A 阈值附近抖动，日志中仍可见少量 `Icpu1=1.00A` 回跳，导致 30 s 计时反复清零。外部最后 10 点为 0.535~0.539 A，均值 0.5358 A/23.5695 W；板端末 20 个 RUN 均值 Vin=23.1585 V、Vout=43.8150 V、Iout=0.4975 A、Pin/Pout=26.740/21.800 W。全程峰值为启动期 1.386 A/60.971 W，最高温度 29.9/29.5 ℃，fault=0，结束 READY/PWM=0/RUN=0。

结论：`FAIL_CV_TERMINATION_THRESHOLD_CHATTER_NO_DONE`，原工况继续锁定。修复轮次 2 只增加一个明确机制：独立的 CV 终止保持状态；首次在 CV 目标附近测到 Iout<=Iterm 后，将请求降到 Iterm 以下 10%（且至少保留 0.02 A、最多为 Iterm 的一半）的裕量，小幅越过阈值只清零计时但不解除降流保持，只有 Iout 超过 Iterm+裕量或电压明显离开 CV 区才恢复正常 PI。DONE 计时仍严格只在实测 Iout<=Iterm 时累计，不把超阈值时间算入 30 s。

### LF-CV-DONE-V24-01 修复轮次 2：测试与构建记录

- 算法版本更新为 `2026-08-19-v45-cv-termination-margin`。主机测试新增 0.49→0.51 A 阈值抖动场景，确认计时在超阈值时清零但终止保持不退出、请求仍不高于 0.45 A；随后实测回到 0.45 A 并保持 30 s 时进入 DONE。43.0 V 明显欠压恢复 PI 的测试继续通过。GCC 严格警告构建和全部主机测试退出码均为 0。
- CPU0 构建通过，text/data/bss=`173742/560/172385` bytes；ELF SHA256=`38D266E2090A638EB5D68657C6C9FCC1BE0BFF8F7147265FDE2C734BA3556B0F`，SREC SHA256=`7E3C268EA4E0EA74B954F0AF8AE80BEA68AE13887AE3E7D9DE09CCC470392695`。
- CPU1 构建通过，text/data/bss=`17174/132/9104` bytes；ELF SHA256=`4B00554F46D68A77E6F88EDF3B939F63159ED2D7228DC56A2DDED9E1E1505249`，SREC SHA256=`802CE9AFE66D4743E4D3ED60754B585AFD3DAC9A9F29A142CFED6FB10F99EC06`。
- 烧录前板端仍为上一轮结束后的 `READY/PWM0/RUN0/fault0`。计划升级当前双核镜像，J-Link 记录路径：`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_v47l_v45_cv_margin_dualcore_flash.log`；电子负载不写入。

### LF-CV-DONE-V24-01 修复轮次 2：升级与复测 PRETEST

J-Link 已完成 CPU0 176128 bytes、CPU1 18432 bytes 的 Erase/Program/Verify，退出码 0。升级后串口确认 `READY/CTRL=OFF/PWM=0/RUN=0/fault=0`、路径全关、ADC valid，说明可进入相同工况复测；不把升级成功记作功能 PASS。

复测 ID：`LF_CV_DONE_V24_01_FIX2`。输入及负载人工工况、板端 profile/调制/PI/slew/cap/power/Vref/AI OFF 与前两轮完全相同，唯一变化为 v45 的终止保持和 0.05 A 请求裕量。计划仍为 50 s；若 DONE 未出现，则继续停在该点定位和修复。

### LF-CV-DONE-V24-01 修复轮次 2：实板结果

本轮仍未通过，而且没有进入 CV：日志中的状态路径仅为 `PRECHARGE→CC`，CV/DONE 计数均为 0。外部最后 10 点为 43.989~43.993 V、0.938~0.939 A，均值 43.9914 V、0.9384 A/41.2810 W；板端末 20 个 RUN 窗口均值 Vin=22.9435 V、Vout=43.5805 V、Iout=0.9020 A、Pin/Pout=44.920/39.395 W，Tpri/Tsec=30.605/29.590 ℃。全程外部峰值 1.376 A/60.556 W，fault=0，结束 READY/PWM=0/RUN=0。

结论：`FAIL_CV_ENTRY_NOT_REACHED_ACTUATOR_SATURATION`。板端用于状态机的 Vout 稳态比 43.80 V 目标低约 0.22 V，也低于当前 43.70 V 的 CV 进入门槛；同时 EPS raw/effective=240/120 ns 已触及 phase cap=120 ns，因此 CC 无法继续把板端采样值推入门槛。外部负载端已经稳定在约 43.99 V，说明该点实际处于 CV 模拟电池端电压附近，但 v45 的终止保持没有机会执行，不能据此评价 v45 通过。

失败工况继续锁定：不得进入 Vin20/18 V 或其他电池/功能测试，电子负载继续保持人工设定的 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON，且只允许低频只读。下一轮先核对板端 Vout 标定、CPU0→CPU1 的 0.1 V 量化和前两轮同工况差值；随后只修改一个最小状态机变量并补边界主机测试。复测仍使用 LFP12、EPS 240/0 ns、1.0 A、Iterm0.5 A、Kp/Ki2.0/0.2、slew0.05 A/s、cap120 ns、100 W、Vref44 V、AI OFF，不通过则继续修复本点。

### LF-CV-DONE-V24-01 修复轮次 3：单变量补偿与构建记录

三次完全相同 CV44 工况中，电子负载与板端末段 Vout 差值分别为 0.2426 V、0.1752 V、0.4114 V；CPU0→CPU1 的 Vout IPC 还以 0.1 V 步进量化。本轮算法版本为 `2026-08-19-v46-cv-sense-compensation`，只增加一个 0.30 V 的充电 CV 反馈补偿常数：补偿后的反馈仅供 CC→CV 下界、CV/float 电压环和终止下界使用；过压上界仍使用未经补偿的板端 Vout，全局 ADC 换算、保护边界、电子负载、EPS 240/0 ns、phase cap 和所有电流环参数均未修改。

测试源码新增 v45 失败复现：CPU1 量化反馈 43.6 V、Iout0.90 A 时必须在 1 s 防抖后进入 CV，随后请求随补偿后的负误差下降并完成原 30 s 终止驻留；43.0 V 的真实明显欠压仍必须保持 CC/恢复电流。当前 PC 没有能生成并运行 Windows 主机可执行文件的 C 编译器，Renesas Clang 21.1.1 只注册 AArch64/ARM 目标，直接主机编译因缺少 Windows C 运行库失败，因此不把该测试写成“已运行通过”；真实 ARM 固件编译链接已经完成，后续必须由本点实板复测验证行为。

- CPU0 构建通过，text/data/bss=`173742/560/172385` bytes；ELF SHA256=`EB9991CA8904349D2E0581902E9184A0E6556FF38E7A0B719FB731CF14F7FAFB`，SREC SHA256=`7E3C268EA4E0EA74B954F0AF8AE80BEA68AE13887AE3E7D9DE09CCC470392695`。CPU0 SREC 与 v45 相同，说明本轮充电算法改动没有改变 CPU0 可烧录内容。
- CPU1 构建通过，text/data/bss=`17198/132/9104` bytes；ELF SHA256=`7CBA199B067E665F4FD70999FDC7A169843FDA6E26AEC4E15E0258F0722D9E08`，SREC SHA256=`2690D4039B613559DA826F1CC7CB2C7C2C85F12B37A5785D73B82BB24F1587CD`。
- `mos_regression_trials.csv` 已重新导入校验，共 25 条记录，修复轮次 2 的 PRETEST 与 FAIL RESULT 字段可正常解析。

下一步仍是原工况：先确认板端 READY/PWM0/RUN0/fault0，再升级当前双核镜像并复核启动状态；之后写入修复轮次 3 的 PRETEST，采集时长延长到 70 s 以覆盖 0.05 A/s 下降过程和 30 s DONE 驻留。电子负载继续只读，不需要用户改变任何设置。

烧录前串口已确认 `READY/CTRL=OFF/PWM=0/RUN=0/fault=00000000`、四路功率路径全关、ADC valid；板端 Vin=23.33 V。计划烧录日志：`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_v47l_v46_cv_sense_comp_dualcore_flash.log`。电子负载不写入，人工 CV44.0 V 工况保持不变。

### LF-CV-DONE-V24-01 修复轮次 3：升级与复测 PRETEST

J-Link 连接 `R7KA8P1KF_CPU1` 成功：CPU0 两个 flash bank 内容一致而跳过；CPU1 Bank0 有 18432 bytes 受影响，Erase/Program/Verify 均为 O.K.，退出码 0。升级后串口确认 `READY/CTRL=OFF/PWM=0/RUN=0/fault=0`、路径全关、ADC valid；此处只证明升级和无功率启动，不作为功能 PASS。

复测 ID：`LF_CV_DONE_V24_01_FIX3`。原人工工况和板端参数继续锁定：输入 24.0 V/5.0 A/ON，RK8510 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON；LFP12、EPS 240/0 ns、Imax/Ipre=1.0/1.0 A、Iterm=0.5 A、Kp/Ki=2.0/0.2、slew0.05 A/s、cap120 ns、100 W、Vref44 V、AI OFF。唯一变化是 v46 的 0.30 V 充电 CV 反馈补偿。计划采集 70 s，原始日志为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_LFP_CV44_EPS_cv_done_1A_r4_v46.log`；RK8510 仅不高于 1 Hz 查询并立即释放 COM24。未出现 CC→CV→DONE 则继续修复本点。

### LF-CV-DONE-V24-01 修复轮次 3：实板 PASS

状态路径完整完成 `PRECHARGE→CC→CV→DONE`：首次 CC 为 22:27:50.807，首次 CV 为 22:27:52.599，首次 DONE 为 22:28:25.141，CV 到 DONE 为 32.543 s，覆盖并超过原 30 s 连续终止驻留；日志共有 69 条 DONE 记录。AI 始终 OFF，requested/effective=EPS/EPS，fault 非零记录为 0，结束 `READY/PWM0/RUN0`。

外部全程峰值 1.371 A/60.314 W；末 10 个电子负载只读样本为 43.988~43.990 V、0.258~0.259 A，均值 43.9898 V、0.2589 A/11.3885 W。板端末 20 个 RUN 窗口均值 Vin=23.1515 V、Vout=43.7390 V、Iout=0.2305 A、Pin/Pout=14.430/10.080 W；末段温度均值 30.33/29.79 ℃，全程最高 30.5/30.0 ℃。DONE 后 CPU1 请求和 CPU0 活动参考均为 0 A，但 EPS 零有效相移仍存在约 0.259 A 的硬件传输下限；该值低于 Iterm0.5 A，符合本阶段“不要求高电流精度、符合充电趋势”的验收边界，精确零电流不在本点范围。

结论：`PASS_LFP_CV_DONE`，原失败工况闭环解除。v46 的 0.30 V 充电 CV 反馈补偿保留为当前正确参数；此前 v44/v45 的失败记录不删除。下一测试仍属于智能充电功能，按计划验证输入电压变化时的充电趋势；进入下一点前先写 PRETEST，并只要求用户调整输入电源，电子负载保持 CV44.0 V 不动。

### CHG-VIN-06：LFP CV/taper 宽输入 22 V PRETEST

测试 ID：`CHG_VIN_06_LFP_CV_V22_01`。本点沿用刚通过的 24 V 低功率状态机参数作为安全起点，只改变输入电源设定 24.0 V→22.0 V：RK8510 继续由用户保持 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON，不对 COM24 执行任何写操作；板端保持 LFP12、EPS 240/0 ns、Imax/Ipre=1.0/1.0 A、Iterm=0.5 A、Kp/Ki=2.0/0.2、slew0.05 A/s、cap120 ns、100 W、Vref44 V、AI OFF。该点是 CHG-VIN-06 宽输入趋势的 22 V 起始点，不替代后续正式电流矩阵。

计划路径必须为 `PRECHARGE→CC→CV→DONE`，全程 fault=0、requested/effective=EPS/EPS，输入变化不得造成阶段乱跳；采集 70 s，以覆盖降流和 30 s 终止驻留。原始日志预留为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN22_EPS_1A_v46_r1.log`，电子负载只做不高于 1 Hz 的短时只读查询。若本点 FAIL，立即锁定 22 V/CV44.0 V 完全相同工况，定位、单变量修复、双核构建升级并复测至 PASS；不得进入 20 V。

### CHG-VIN-06：LFP CV/taper 宽输入 22 V 实测 FAIL

本点未通过，工况立即锁定，禁止进入 20 V。状态路径为 `PRECHARGE→CC→CV`：首次 CC 22:33:32.581，首次 CV 22:33:34.142，CV 记录 148 条，但 DONE 为 0。AI 始终 OFF，requested/effective=EPS/EPS，fault 非零记录为 0；测试结束板端回到 `READY/PWM0/RUN0`，电子负载保持用户原设置不变。

外部峰值仅 0.112 A/4.923 W；末 10 个只读样本为 43.146~43.148 V、均为 0.004 A，均值 43.1466 V/0.0040 A/0.1720 W。板端末 20 个 RUN 窗口均值 Vin=21.1140 V、Vout=42.7290 V、Iout=-0.0250 A、Pin/Pout=1.895/-1.055 W；末段温度均值 30.33/29.72 ℃，全程最高 30.5/30.0 ℃。EPS raw=240 ns、effective phase=120 ns 持续等于 phase cap=120 ns，执行器已饱和，但输出端未建立到 CV44.0 V，因此不能把“处于 CV 状态”误判为充电趋势通过。

结论：`FAIL_CV_DONE_NOT_REACHED_PHASE_CAP_SATURATION`。下一步先检索既有 22 V 数据选择相邻已验证的 phase-cap 起点；复测必须继续保持输入 22.0 V/5.0 A/ON、RK8510 CV44.0 V/OCP3.0 A/OPP120 W/LOAD ON、LFP12、EPS 240/0 ns、1.0 A、Iterm0.5 A、Kp/Ki2.0/0.2、slew0.05 A/s、100 W、Vref44 V、AI OFF。仅允许改变 phase cap 一个变量；若仍 FAIL，继续留在本点修复。

### CHG-VIN-06 22 V 修复轮次 1：历史数据选点与 PRETEST

复查 `2026-08-17_v44_advanced_control/WORKLOG.md` 与 `advanced_control_trials.csv`：在电源面板约 22 V、板端 Vin=21.0729 V、CV43/1 A 的既有实测中，EPS 240/0 ns、cap180 ns 只能得到约 0.67 A，继续把 cap 增到 240 ns 后反降到约 0.60 A，说明该调制区并非靠继续放大 cap 就能安全恢复；最终 EPS 0/52 ns、cap180 ns、Kp/Ki2.0/0.2 获得 Iout=0.99663 A、fault=0 的 PASS。当前 22 V 失败点板端 Vin=21.114 V，与该历史点相邻，因此以 0/52 ns 作为有实测依据的安全修复起点。

修复轮次 ID：`CHG_VIN_06_LFP_CV_V22_01_FIX1`。只改变一个控制变量——EPS 内移相组合由 240/0 ns 改为 0/52 ns；phase cap 暂时仍为 120 ns，避免同时放宽执行器边界。输入 22.0 V/5.0 A/ON、RK8510 CV44.0 V/OCP3.0 A/OPP120 W/LOAD ON、LFP12、Imax/Ipre=1.0/1.0 A、Iterm0.5 A、Kp/Ki2.0/0.2、slew0.05 A/s、100 W、Vref44 V、AI OFF 全部保持。预期路径仍必须完整达到 `PRECHARGE→CC→CV→DONE`；原始日志预留为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN22_EPS_1A_v46_r2_inner0_52.log`。若不通过，继续锁定本点。

### CHG-VIN-06 22 V 修复轮次 1：ADC 预检无效、未启动

RK8510 低频只读确认 CV44.0 V、LOAD ON；板端 READY、CTRL OFF、fault=0，Vin=21.37 V。第一次 `adcdiag` 的四个电流零点 P000/P002/P006/P008 均在 1.643~1.650 V 正常范围，但同时报告 `invalid=0x0040`，脚本按预检门禁立即退出。PWM、功率路径和 0/52 ns 修复参数均未启动，最终仍为 `READY/PWM0/RUN0`，因此该次记录为 `INVALID_PRECHECK_ADC_MASK_0X0040_NO_PWM`，不能评价修复参数通过或失败。

原工况继续锁定。下一步只对 COM4 做多次 `show/adcdiag` 读取，确认 invalid 位是单帧瞬态还是持续异常；不访问或控制电子负载。只有连续 ADC 诊断恢复有效后，才使用完全相同的 FIX1 参数重新采集，日志另开 r3，不能覆盖本次无效原始记录。

COM4 连续 6 次 `adcdiag` 已恢复 `invalid=0x0000`，P000/P002/P006/P008 始终处于 1.640~1.654 V，板端持续 READY、fault=0；据此将 r2 的 0x0040 定性为单帧瞬时预检无效，仍保留原始记录。已创建 `CHG_VIN_06_LFP_CV_V22_01_FIX1_RETRY1` PRETEST：不改变任何物理或控制参数，继续使用 EPS 0/52 ns、cap120 ns，在完全相同 22 V/CV44.0 V 工况采集 70 s。新日志为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN22_EPS_1A_v46_r3_inner0_52.log`。

### CHG-VIN-06 22 V 修复轮次 1：实板 PASS

r3 完整完成 `PRECHARGE→CC→CV→DONE`：首次 PRECHARGE 22:40:21.152、CC 22:40:22.588、CV 22:40:24.127、DONE 22:40:55.840，CV→DONE 为 31.714 s；共有 68 条 DONE 记录。AI 始终 OFF，requested/effective=EPS/EPS，fault 非零记录为 0，结束为 `READY/PWM0/RUN0`。

外部全程峰值 0.493 A/21.688 W；末 10 个 RK8510 低频只读样本为 43.988~43.990 V、0.492~0.493 A，均值 43.9894 V/0.4927 A/21.6735 W。板端末 20 个 RUN 窗口均值 Vin=21.1340 V、Vout=43.7170 V、Iout=0.4570 A、Pin/Pout=24.360/19.985 W；末段温度均值 29.92/29.44 ℃，全程最高 30.1/30.3 ℃。

结论：`PASS_LFP_CV_DONE_VIN22`。EPS 0/52 ns 在该输入比下形成 26 ns 的有效相移下限，DONE 后外部保持约 0.493 A，低于 Iterm0.5 A 且符合本阶段充电降流趋势验收；不把它误写成精准电流结果。原 22 V 失败点已闭环解除，保留 cap120 ns，不需要放宽相移上限。进入 20 V 前仍须先创建新 PRETEST，并只让用户手动改变输入电源。

### CHG-VIN-06：LFP CV/taper 宽输入 20 V PRETEST

测试 ID：`CHG_VIN_06_LFP_CV_V20_01`。既有 v44a 数据表明，电源面板约 20 V、板端 Vin=19.12966 V、CV43/1 A 时，EPS 0/52 ns 在正相移区失去可控窗口，而 TPS 100/564 ns、forward、cap260 ns 已实测 Iout=0.99858 A、fault=0、闭环 PASS。为避免重复进入已知无效 EPS 区，本点直接采用该相邻 PASS 调制参数作为安全起点；不是把旧 CV43 结果冒充当前 CV44 结果。

本点人工工况：输入电源仅从 22.0 V 改为 20.0 V，限流继续 5.0 A、输出保持 ON；RK8510 不改变，继续 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON。板端采用 LFP12、TPS 100/564 ns、forward、Imax/Ipre=1.0/1.0 A、Iterm0.5 A、Kp/Ki2.0/0.2、保守 slew0.05 A/s、cap260 ns、100 W、Vref44 V、AI OFF。必须完成 `PRECHARGE→CC→CV→DONE` 且 fault=0；原始日志预留为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN20_TPS_100_564_1A_v46_r1.log`。若 FAIL，锁定 20 V 原工况修复，不进入 18 V。

用户完成 20.0 V 手动切换后，测试主机脚本发现仍保留旧的固定预检条件 `Vin=20..28 V`，会把本轮以及后续 18 V 工况在 PWM 前误判无效。已仅修改 `tools/Run-DabCurrentTest.ps1` 的主机预检：新增显式 `ExpectedInputVoltageV=18..24 V` 参数，并要求板端读数与人工设定相差不超过 2.0 V；运行期 10.5 A 输出、19.5 A 输入、55 V 输出边界、COM24 最大 1 Hz 只读策略及所有板端命令均未改变。脚本 SHA256=`15A320DA8833B77FA3171BA2E2FDE75B5977F4B4A83540CCE001CF99A499BA15`。本点调用时显式传入 20.0 V。

### CHG-VIN-06：LFP CV/taper 宽输入 20 V 实测 FAIL，锁定修复

`CHG_VIN_06_LFP_CV_V20_01` 未通过，保持输入 20.0 V、电子负载 CV44.0 V 的完全相同工况，不进入 18 V。70 s 采集中，负载只读样本 64 个，外部峰值仅 0.004 A/0.156 W；末 10 个样本均值为 39.1152 V/0.0040 A/0.1560 W。板端末 20 个功率窗口均值 Vin=19.2415 V、Vout=38.8770 V、Iout=-0.0210 A、Pin/Pout=3.455/-0.795 W；温度末段均值 29.735/31.155 ℃，最高 29.9/31.7 ℃，全程 fault=0，结束为 `READY/PWM0/RUN0`。

充电状态机从 PRECHARGE 进入 CC，但 157 条功率遥测全部停在 SOFTSTART，Vout 未达到 RUN 门槛 `0.9*44=39.6 V`。虽然请求 TPS 100/564 ns，但 effective TPS 记录为 0，实际始终 SPS、inner=0/0 ns、effective phase=4 ns。失败定性为 `FAIL_TPS_SOFTSTART_INNER_NOT_APPLIED_RUN_NOT_REACHED`：v47l 的硬启动包络把“非 RUN 状态”无条件固定为 SPS，而历史 v44a 已验证的低输入 TPS 参数在进入 RUN 之前没有获得渐进生效机会，形成启动死锁。

原始日志保存在 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN20_TPS_100_564_1A_v46_r1.log`，失败数据不覆盖。下一步只修复启动阶段的调制生效机制：保留初始 SPS/inner=0 的安全保持和相移斜坡，不降低电流/电压边界，不改变电子负载；经双核构建升级后，必须在同一 20 V/CV44.0 V/TPS100/564 ns 工况复测至 PASS。

### v47m 单变量启动修复

只修改 `dab_modulation_targets()` 的 SOFTSTART 释放条件：所有模式仍先执行 1000 ms 的 SPS/inner=0 安全保持；保持结束后，仅当请求为显式 EPS/TPS 且已配置 manual inner 时，才允许内相移在 SOFTSTART 中按现有 `0.020 ns/ms` 慢斜率建立。AUTO 仍只能在 RUN 中分区，SPS、RUN 门槛、有效外相移 4 ns 启动上限、4 ns/100 ms 的 RUN 后释放、硬件输出时序和故障边界均不改变。该机制是对 v44a 已实板验证的 manual-inner 启动死锁修复的保守恢复，斜率比 v44a 的 0.04 ns/ms 再减半；固件标识更新为 `2026-08-19-dualcore-safephase-v47m`。

CPU0 ARM 目标完整构建通过，text/data/bss=`173830/560/172385` bytes；ELF SHA256=`8BCE97A93FB663D843FD208198FD90E9C9B1DD527726F852AEE9170FBC19631B`，SREC SHA256=`F3605B2544A81544B81F8E507683A30B66F301A72C3FDAC9270298293DB385ED`。CPU1 依赖重建通过，text/data/bss=`17198/132/9104` bytes；ELF SHA256=`7CBA199B067E665F4FD70999FDC7A169843FDA6E26AEC4E15E0258F0722D9E08`，SREC SHA256=`2690D4039B613559DA826F1CC7CB2C7C2C85F12B37A5785D73B82BB24F1587CD`。本机仍没有可用 Windows 主机 C 编译器，因此不把 ARM 构建冒充主机单元测试。

烧录前仅通过 COM4 执行 `show/adcdiag` 只读确认：板端 `READY/CTRL=OFF/PWM=0/RUNstate=READY/fault=0`，四路路径全关，ADC `valid=1/invalid=0x0000`，Vin=19.39 V；当前 TPS100/564 ns、current PI2.0/0.2、slew0.05 A/s、cap260 ns 参数仍在 RAM 状态中。电子负载未访问、未写入。只读原始记录为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_v47m_preflash_readonly.log`。

第一次 J-Link 主机调用因 CommanderScript 路径含空格且引用未保留，在连接探针之前退出，退出码 1；失败原文保留为 `2026-08-19_v47m_manual_softstart_dualcore_flash.attempt1.log`，未复位或写入板端。修正引用后重新执行，退出码 0：CPU0 Bank0 擦写/编程/校验 176128 bytes，Bank1 内容一致；CPU1 Bank0 内容一致。完整原始输出为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_v47m_manual_softstart_dualcore_flash.log`。

烧录后 COM4 只读确认固件回到 `READY/CTRL=OFF/PWM=0/RUNstate=READY/fault=0`，四路功率路径全关，ADC `valid=1/invalid=0x0000`，Vin=19.40 V。电子负载未访问。记录为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_v47m_postflash_readonly.log`。已创建 `CHG_VIN_06_LFP_CV_V20_01_FIX1` PRETEST：保持输入 20.0 V、RK8510 CV44.0 V、TPS100/564 ns、forward、Imax/Ipre1.0/1.0 A、Iterm0.5 A、PI2.0/0.2、slew0.05 A/s、cap260 ns、P100 W、AI OFF，仅固件改为 v47m。新原始日志为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN20_TPS_100_564_1A_v47m_r2.log`。

### CHG-VIN-06 20 V 修复轮次 1：v47m 实板 PASS

v47m 在相同工况中解除启动死锁。状态机首次 PRECHARGE 22:54:57.846、CC 22:54:59.298、CV 22:55:20.416、DONE 22:56:01.362，CV→DONE 为 40.945 s，共保留 15 条 DONE 记录。板端在 22:55:06.097 进入 RUN，requested/effective=TPS/TPS 共解析到 159 条，最终 manual inner 完整达到 100/564 ns；AI 全程 OFF，fault 非零记录为 0，结束回到 `READY/PWM0/RUN0`。

64 个 RK8510 低频只读样本中，外部峰值 0.875 A/38.493 W；末 10 点为 43.988~43.990 V、0.295~0.313 A，均值 43.9896 V/0.2996 A/13.1790 W，低于 Iterm0.5 A 并符合 CV 后降流终止趋势。板端末 20 个 RUN 窗口均值 Vin=18.9955 V、Vout=43.4695 V、Iout=0.2690 A、Pin/Pout=17.170/11.710 W；末段温度均值 30.340/30.975 ℃，全程最高 30.6/31.2 ℃。

结论：`PASS_LFP_CV_DONE_VIN20_V47M`，原 20 V 失败已闭环。脚本 `in_band=0` 是恒流精度 ±0.1 A 统计，不是本智能充电工况的判据；本点按完整状态路径、CV 稳压、Iterm 以下降流、DONE、fault0 判定。下一点仍属 CHG-VIN-06，输入电源需要由用户从 20.0 V 手动改为 18.0 V；RK8510 保持 CV44.0 V/OCP3 A/OPP120 W/LOAD ON，不作任何写入。

### CHG-VIN-06：LFP CV/taper 宽输入 18 V PRETEST

测试 ID：`CHG_VIN_06_LFP_CV_V18_01`。人工只把输入电源从 20.0 V 改为 18.0 V，限流保持 5.0 A、输出保持 ON；RK8510 继续由用户保持 CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON。根据 v44a 已验证的板端 Vin=18.003 V、CV43/1 A、TPS100/564 ns、cap268 ns、Iout=0.99901 A、fault0 数据，本点保守沿用 TPS100/564 ns、forward、PI2.0/0.2，只把相移上限由 20 V 点的 260 ns 改为有实测依据的 268 ns。Imax/Ipre=1.0/1.0 A、Iterm0.5 A、slew0.05 A/s、P100 W、Vref44 V、AI OFF 不变。

必须完成 `PRECHARGE→CC→CV→DONE`，requested/effective=TPS/TPS、fault=0，输出电流随进入 CV 和 DONE 合理下降；采集延长为 75 s。原始日志预留为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN18_TPS_100_564_CAP268_1A_v47m_r1.log`。若失败，锁定 18 V/CV44 V 工况逐项修复，不进入下一功能。已停止仪器访问，为用户保留不少于 60 s 的手动操作时间。

### CHG-VIN-06 18 V 首轮实测 FAIL

面板 18.0 V 对应板端空载 Vin=17.41 V、加载末段 Vin=17.1725 V，低于历史种子板端 Vin=18.003 V。v47m 启动修复正常：requested/effective=TPS/TPS，内相移最终达到 100/564 ns并进入 RUN；但有效相移持续饱和在 cap268 ns，共解析到 71 条饱和记录。外部 69 个只读样本峰值仅 0.004 A/0.161 W；末 10 点 41.849~41.858 V、均为 0.003 A，均值 41.8537 V/0.0030 A/0.1250 W。板端末 20 个运行窗口均值 Vin=17.1725 V、Vout=41.5195 V、Iout=-0.0365 A、Pin/Pout=4.820/-1.515 W；温度末段均值31.510/31.425 ℃，最高31.9/31.7 ℃，fault=0并安全回到 `READY/PWM0/RUN0`。

状态机只有 `PRECHARGE→CC`，CV=0、DONE=0，结论为 `FAIL_CV_NOT_REACHED_PHASE_CAP_268_SATURATION`。保持输入18.0 V、RK8510 CV44.0 V和全部控制参数不变，只将 phase cap 增加一个4 ns量化的双步进：268→276 ns；仍低于320 ns常规上限。修复轮次 `CHG_VIN_06_LFP_CV_V18_01_FIX1` 的原始日志预留为 `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN18_TPS_100_564_CAP276_1A_v47m_r2.log`。若仍不通过，继续锁定本点，每次只增加一个8 ns步进。

### CHG-VIN-06 18 V 修复轮次 1 FAIL，继续锁点

`CHG_VIN_06_LFP_CV_V18_01_FIX1` 只把 phase cap 从268 ns提高到276 ns，其余输入18.0 V、RK8510 CV44.0 V、TPS100/564 ns、1.0 A、PI2.0/0.2、slew0.05 A/s、P100 W、AI OFF全部不变。v47m 启动机制正常，requested/effective=TPS/TPS，内相移到达100/564 ns并进入RUN；但有效相移276 ns饱和记录177条，仍未得到进入CV所需的输出能力。

69个负载低频只读样本峰值0.004 A/0.160 W，末10点均值42.2391 V/0.0030 A/0.1260 W。板端末20个功率窗口均值Vin=17.0730 V、Vout=41.7750 V、Iout=-0.0255 A、Pin/Pout=7.270/-1.065 W；最高温度33.3/32.8 ℃，fault非零记录0，结束安全回到`READY/PWM0/RUN0`。状态路径仍仅为`PRECHARGE→CC`，因此判定`FAIL_CV_NOT_REACHED_PHASE_CAP_276_SATURATION`，失败原始日志永久保留。

创建修复轮次`CHG_VIN_06_LFP_CV_V18_01_FIX2`：继续保持完全相同的18 V/CV44 V工况，只将phase cap从276 ns增至284 ns；仍采用8 ns单变量保守步进，不改变电流环、内相移、功率上限、充电阈值或AI状态。原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN18_TPS_100_564_CAP284_1A_v47m_r3.log`。本轮若不通过，继续锁定本测试点修复，不进入后续工况。

FIX2首次主机调用在PowerShell参数绑定阶段因传入不存在的`BaudRate`参数而退出，未进入脚本、未访问板端或负载。随后去掉该无效参数，但遗漏显式`ExpectedLoadVoltageV=44`：脚本只读得到RK8510 mode=CV、state=1、CV=44 V后，被默认期望43 V拒绝；未执行板端参数设置、未启动PWM，负载未写入或关闭，清理仅使板端保持`READY/PWM0/RUN0/fault0`。该轮标记`INVALID_HOST_EXPECTED_LOAD_VOLTAGE_OMITTED`，原始日志r3保留，不参与调参判定。

新建`CHG_VIN_06_LFP_CV_V18_01_FIX2_RETRY1`，显式补充主机预检参数`ExpectedLoadVoltageV=44`，板端和物理工况仍与FIX2完全相同，phase cap仍为284 ns。原始日志改为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN18_TPS_100_564_CAP284_1A_v47m_r4.log`，避免覆盖无效轮次。

### CHG-VIN-06 18 V 修复轮次 2 FAIL，继续锁点

cap284 ns有效重测完整执行75 s，TPS启动、进入RUN和100/564 ns内相移均正常，但有效相移284 ns持续饱和179条。69个负载低频只读样本峰值0.004 A/0.161 W，末10点均值42.7261 V/0.0030 A/0.1280 W；板端末20个窗口均值Vin=17.0755 V、Vout=42.2660 V、Iout=-0.0285 A、Pin/Pout=6.270/-1.210 W。最高温度33.3/32.7 ℃、fault非零记录0，结束为`READY/PWM0/RUN0`。

状态仍只有`PRECHARGE→CC`，判定`FAIL_CV_NOT_REACHED_PHASE_CAP_284_SATURATION`。原始日志r4保留。下一轮`CHG_VIN_06_LFP_CV_V18_01_FIX3`仍锁定相同输入18.0 V、负载CV44.0 V及全部控制条件，只把phase cap从284 ns增至292 ns；新日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN18_TPS_100_564_CAP292_1A_v47m_r5.log`。

### CHG-VIN-06 18 V 修复轮次 3 FAIL，继续锁点

cap292 ns轮次仍只到`PRECHARGE→CC`。外部69点峰值0.004 A/0.160 W，末10点均值43.2725 V/0.0030 A/0.1290 W；板端末20点均值Vin=17.0895 V、Vout=42.8035 V、Iout=-0.0375 A、Pin/Pout=6.655/-1.585 W。TPS100/564 ns正常，cap292 ns饱和182条，最高温度34.6/33.5 ℃，fault=0并回到`READY/PWM0/RUN0`。

结论`FAIL_CV_NOT_REACHED_PHASE_CAP_292_SATURATION`，日志r5保留。创建`CHG_VIN_06_LFP_CV_V18_01_FIX4`，仍锁定原工况与全部控制参数，只将cap292→300 ns；新日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN18_TPS_100_564_CAP300_1A_v47m_r6.log`。

### CHG-VIN-06 18 V 修复轮次 4 PASS

cap300 ns在同一18 V/CV44 V工况完成全部状态路径：PRECHARGE 23:10:40.139、CC 23:10:41.582、CV 23:11:20.371、DONE 23:11:50.256，CV→DONE 29.885 s，共12条DONE记录。requested/effective=TPS/TPS解析178条，内相移最终100/564 ns，AI OFF、fault非零记录0，结束回到`READY/PWM0/RUN0`。

69个RK8510低频只读样本峰值0.004 A/0.161 W；末10点均值43.7789 V/0.0030 A/0.1310 W，低于Iterm0.5 A并符合CV末期终止趋势。板端末20个窗口均值Vin=17.2395 V、Vout=43.5125 V、Iout=-0.0255 A、Pin/Pout=8.120/-1.125 W；最高温度35.5/34.0 ℃。结论`PASS_LFP_CV_DONE_VIN18_CAP300`，18 V失败闭环解除；本条件正确参数为TPS100/564 ns、cap300 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V。

### CHG-VIN-06 宽输入返回段 20 V PRETEST

按计划继续18→20→22→24 V返回段，先建立`CHG_VIN_06_LFP_CV_V20_RETURN_01`。用户只把输入电源改为20.0 V/5.0 A/ON；RK8510保持CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON，主机在用户确认完成前不访问仪器。板端恢复已经在同一20 V/CV44 V工况通过的TPS100/564 ns、cap260 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、Imax/Ipre1.0 A、Iterm0.5 A、AI OFF参数。验收仍为`PRECHARGE→CC→CV→DONE`、fault0和合理降流；75 s原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN20_RETURN_TPS_100_564_CAP260_1A_v47m_r7.log`。

### CHG-VIN-06 返回段20 V首轮受人工调压干扰，无效重测

本轮运行中一度观察到CV电流由约0.7 A回升、外部峰值2.025 A/89.077 W，同时板端Vin约19.1→21.1→19.2 V。用户随后确认该变化期间正在手动调整输入电压，因此不能把电流变化归因为控制算法，也不能作为TPS100/564参数失败证据。本轮最终状态路径`PRECHARGE→CC→CV`、fault0，结束`READY/PWM0/RUN0`；原始日志r7完整保留为受扰动输入数据，结果标记`INVALID_MANUAL_INPUT_VOLTAGE_CHANGED_DURING_RUN`并排除在20 V稳态调参结论之外。

创建`CHG_VIN_06_LFP_CV_V20_RETURN_01_RETRY1`：参数完全不变，仍为TPS100/564 ns、cap260 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、AI OFF。用户须把输入电源固定在20.0 V/5.0 A/ON并在完整75 s中不再调节；负载保持CV44.0 V。新日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN20_RETURN_TPS_100_564_CAP260_1A_v47m_r8.log`。

### CHG-VIN-06 返回段 20 V 稳定重测 PASS

`CHG_VIN_06_LFP_CV_V20_RETURN_01_RETRY1`按完全相同的TPS100/564 ns、cap260 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、1.0 A/Iterm0.5 A、AI OFF参数完整执行75 s。RUN阶段板端Vin为19.14~19.35 V，未再出现上一轮人工调压造成的输入突变。

状态路径为PRECHARGE 23:18:04.511、CC 23:18:05.961、CV 23:18:27.047、DONE 23:19:07.959，CV→DONE 40.911 s，共25条DONE记录。69个RK8510低频只读样本峰值0.904 A/39.769 W；末10点稳定为43.9912 V/0.3189 A/14.0286 W，电流范围0.318~0.319 A，低于Iterm0.5 A并符合CV后降流终止趋势。板端末20个运行窗口均值Vin=19.2810 V、Vout=43.8500 V、Iout=0.2925 A、Pin/Pout=18.385/12.800 W；全程最高温度33.6/33.2 ℃。requested/effective=TPS/TPS记录156条，fault非零记录0，结束回到`READY/PWM0/RUN0`。

结论为`PASS_LFP_CV_DONE_VIN20_RETURN`，正确参数立即固化为20 V点TPS100/564 ns、cap260 ns；受扰动r7仍永久保留但不参与结论。20 V返回点通过后方可进入22 V返回点。

### CHG-VIN-06 宽输入返回段 22 V PRETEST

测试ID为`CHG_VIN_06_LFP_CV_V22_RETURN_01`。只允许用户把输入电源从20.0 V改为22.0 V，限流保持5.0 A、输出保持ON；RK8510继续保持CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON，主机不写入电子负载。板端采用此前同一22 V/CV44 V点已经通过的EPS0/52 ns、cap120 ns，其余为forward、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、Imax/Ipre1.0 A、Iterm0.5 A、AI OFF。

验收要求仍为`PRECHARGE→CC→CV→DONE`、requested/effective=EPS/EPS、输出电流在CV后合理下降、fault=0并最终回到READY；采集75 s。原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN22_RETURN_EPS_0_52_CAP120_1A_v47m_r9.log`。若不通过，锁定22 V/CV44 V工况逐项修复，不进入24 V。

### CHG-VIN-06 返回段 22 V 首轮 FAIL，锁点修复

`CHG_VIN_06_LFP_CV_V22_RETURN_01`完整执行75 s，状态为PRECHARGE 23:23:04.190、CC 23:23:05.239、CV 23:23:17.831，但DONE记录为0。69个负载低频只读样本峰值0.656 A/28.858 W；末10点43.9904 V/0.5134 A/22.5843 W，电流范围0.499~0.522 A，仍略高于Iterm0.5 A。板端末20个窗口均值Vin=20.8645 V、Vout=43.3785 V、Iout=0.4840 A、Pin/Pout=25.470/21.005 W；最高温度32.2/32.2 ℃，fault非零记录0，结束回到`READY/PWM0/RUN0`。

requested/effective=EPS/EPS共181条，其中有效相移26 ns记录105条、30 ns记录48条，说明末段被EPS最小传输地板限制。历史EPS0/52 ns通过轮末10点电流0.4927 A，当前0.5134 A暴露该参数靠近Iterm边缘，不能将本轮判为通过。结论`FAIL_DONE_NOT_REACHED_EPS_FLOOR_MARGINAL`，不进入24 V。

修复轮次`CHG_VIN_06_LFP_CV_V22_RETURN_01_FIX1`保持输入22.0 V、RK8510 CV44.0 V、cap120 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、1.0 A/Iterm0.5 A和AI OFF全部不变，只把EPS次级内相移52→44 ns，预计把最小传输地板向下移动且不增加MOS应力。新日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN22_RETURN_EPS_0_44_CAP120_1A_v47m_r10.log`。必须原点跑到DONE后才解除锁点。

### 新增智能充电最终交付验收要求

操作者明确要求最终展示时亲自改变RK8510 CV电压，输出电流必须随模拟电池电压按友好充电规律变化；随后把RK8510固定在某个已经通过的CV点，由操作者把输入电压在18~24 V之间改变，输出电流必须保持不变。计划已增加DEMO-CHG-01~05，并把固定CV下的量化判据收紧为相对24 V基准`max(0.05 A, 5%)`，模式可按本地表无扰切换但电流和阶段不能被Vin扰动。

计划同时增加LFP可交付固件门禁：LFP状态机、宽输入、三模式、本地自动选择、固定CV电流不变性及串口上位机全部通过后，立即固化参数、构建和烧录CPU0/CPU1，并在新固件上复测LFP代表工况；不能等到整个智能充电阶段结束，也不能只把正确参数保存在主机脚本中。最终人工展示期间主机对RK8510仍保持只读且不高于1 Hz，每次人工操作前停止访问并留出至少60 s。

### 智能充电电池范围缩减为两类

为节省时间，操作者将智能充电范围收敛为最常用的Li-ion 10S和LiFePO4 12S。计划版本更新为v5，已删除Lead-acid状态机、PB-01~09、CHG-VIN-07~09、铅酸最终展示和串口选择项；后续不再为铅酸采集或调参。Li-ion/LFP仍必须分别完成状态机、宽输入电流不变性、三模式排名、AI OFF自动选择、人工CV轨迹展示和串口上位机回归，LFP完成后立即执行固件升级门禁。

### CHG-VIN-06 返回段 22 V 修复轮 PASS

`CHG_VIN_06_LFP_CV_V22_RETURN_01_FIX1`只把EPS次级内相移52→44 ns，其余工况和参数不变。状态路径为PRECHARGE 23:26:23.716、CC 23:26:25.140、CV 23:26:26.793、DONE 23:26:58.359，CV→DONE 31.566 s，共80条DONE记录。69个RK8510低频只读样本峰值0.374 A/16.453 W；末10点稳定为43.9909 V/0.3734 A/16.4259 W，电流范围0.373~0.374 A，明确低于Iterm0.5 A。

板端末20个运行窗口均值Vin=21.1485 V、Vout=43.7190 V、Iout=0.3350 A、Pin/Pout=19.175/14.665 W；最高温度32.2/33.4 ℃。requested/effective=EPS/EPS共169条，其中158条为新的22 ns最小有效相移，fault非零记录0，最终`READY/PWM0/RUN0`。结论`PASS_LFP_CV_DONE_VIN22_RETURN_INNER44`，正确22 V参数更新为EPS0/44 ns、cap120 ns；原0/52 ns参数降级为边缘历史点，不再作为正式22 V终止参数。

### CHG-VIN-06 宽输入返回段 24 V PRETEST

最后一个返回点为`CHG_VIN_06_LFP_CV_V24_RETURN_01`。操作者只把输入电源从22.0 V改为24.0 V，限流保持5.0 A、输出保持ON；RK8510继续保持CV44.0 V、OCP3.0 A、OPP120 W、LOAD ON且主机不写入。板端恢复此前同一24 V/CV44 V已经通过的EPS240/0 ns、cap120 ns，其余保持PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、Imax/Ipre1.0 A、Iterm0.5 A、AI OFF。

必须再次完成`PRECHARGE→CC→CV→DONE`、requested/effective=EPS/EPS、fault0并回到READY；失败则锁定24 V修复。75 s原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN06_LFP_CV44_VIN24_RETURN_EPS_240_0_CAP120_1A_v47m_r11.log`。

### CHG-VIN-06 返回段 24 V PASS，CV/taper 往返链闭环

`CHG_VIN_06_LFP_CV_V24_RETURN_01`按历史已通过的EPS240/0 ns、cap120 ns执行。状态路径为PRECHARGE 23:33:00.970、CC 23:33:01.962、CV 23:33:04.170、DONE 23:33:36.800，CV→DONE 32.630 s，共75条DONE记录。69个RK8510低频只读样本峰值1.371 A/60.355 W；末10点稳定为43.9920 V/0.2511 A/11.0459 W，低于Iterm0.5 A并保持合理降流。

板端末20个运行窗口均值Vin=23.0250 V、Vout=43.5830 V、Iout=0.2230 A、Pin/Pout=13.975/9.710 W；最高温度32.0/31.9 ℃。requested/effective=EPS/EPS共169条，fault非零记录0，结束回到`READY/PWM0/RUN0`。结论`PASS_LFP_CV_DONE_VIN24_RETURN`，CHG-VIN-06的24→22→20→18→20→22→24 V链闭环完成；正确点参数分别保留在逐点记录中，不能把单一参数束误用于全部Vin。

### CHG-VIN-05 LFP 固定 CV 电流不变性：24 V 基准 PRETEST

LFP CV/taper链完成后，按新增硬验收转入固定RK8510 CV下的输入电压不变性。先建立`CHG_VIN_05_LFP_CC_CV43_V24_BASELINE_01`：操作者把RK8510从CV44.0 V手动改为CV43.0 V，LOAD保持ON；输入电源保持24.0 V/5.0 A/ON。沿用历史`LF_CC_V24_06`已经得到外部1.0007 A、fault0的EPS240/0 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、Imax/Ipre1.0 A、Iterm0.5 A、AI OFF参数。

本点运行45 s，要求状态稳定在CC、输出正向、fault0并保存末10 s作为24 V基准。后续负载CV43.0 V和充电目标全程不变，只让操作者按24→22→20→18→20→22→24 V改变输入；每点Iout相对本基准偏差必须不超过`max(0.05 A, 5%)`。原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN24_EPS_240_0_CAP120_1A_v47m_r12.log`。

### CHG-VIN-05 LFP CC 24 V 基准 PASS

v47m在输入24 V、RK8510 CV43.0 V下复现历史EPS240/0 ns、cap120 ns安全点。42个外部只读样本峰值1.032 A/44.360 W；末10点均值42.9846 V/1.0302 A/44.2822 W，范围1.029~1.032 A。板端末20个运行窗口均值Vin=22.9430 V、Vout=42.6100 V、Iout=1.0010 A、Pin/Pout=48.310/42.620 W；最高温度32.0/31.2 ℃。状态`PRECHARGE→CC`并稳定保持CC，EPS有效117条，fault0，最终`READY/PWM0/RUN0`。

结论`PASS_LFP_CC_CV43_VIN24_BASELINE`。后续Vin点以外部1.0302 A为基准，5%比0.05 A更大，因此正式允许区间为0.9787~1.0817 A。

### CHG-VIN-05 LFP CC 固定 CV43：22 V PRETEST

测试ID`CHG_VIN_05_LFP_CC_CV43_V22_01`。操作者只把输入电源从24.0 V改为22.0 V，限流保持5.0 A、输出ON；RK8510必须继续CV43.0 V、LOAD ON且不作其他变化。板端从22 V已通过的安全分区EPS0/44 ns、cap120 ns开始，PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、1.0 A充电目标和AI OFF不变。

运行45 s，要求稳定在CC、末10 s外部Iout在0.9787~1.0817 A、fault0并回到READY。原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN22_EPS_0_44_CAP120_1A_v47m_r13.log`。若不通过，锁定22 V/CV43 V修复，不进入20 V。

### CHG-VIN-05 LFP CC 固定 CV43：22 V PASS

`CHG_VIN_05_LFP_CC_CV43_V22_01`按预先登记的EPS0/44 ns、cap120 ns安全参数完成45 s实测。42个RK8510低频只读样本峰值1.038 A/44.618 W；末10点均值42.9856 V/1.0357 A/44.5197 W，电流范围1.033~1.038 A，完整落在24 V基准确定的0.9787~1.0817 A允许区间内。

板端末20个运行窗口均值Vin=21.130 V、Vout=42.848 V、Iout=1.002 A、Pin/Pout=47.135/42.980 W；最高温度31.7/31.5 ℃。状态`PRECHARGE→CC`并保持CC，requested/effective=EPS/EPS共110条，末段raw phase 8~12 ns、effective phase 30~34 ns，fault非零记录0，最终`READY/PWM0/RUN0`。AI保持OFF，证明本地确定性充电控制在22 V点正常工作；结论`PASS_LFP_CC_CV43_VIN22_IN_BAND`，允许进入20 V点。

本点1 A由串口配置的充电`Imax/Ipre=1.00 A`及CPU1电流目标产生，日志明确显示`Icmd/Iactive/Icpu1=1.00/1.00/1.00 A`；RK8510仅提供CV43.0 V负载条件。改变串口目标应改变PRECHARGE/CC电流，但其他目标值及最终0~7 A能力必须在对应精准电流工况逐点实测，CV阶段仍应由状态机主动降流。

### CHG-VIN-05 LFP CC 固定 CV43：20 V PRETEST

测试ID`CHG_VIN_05_LFP_CC_CV43_V20_01`。操作者只把输入电源从22.0 V改为20.0 V，限流保持5.0 A、输出保持ON；RK8510继续保持CV43.0 V、LOAD ON，主机不写入。板端从同一20 V/CV44 V已经双向通过的TPS100/564 ns、cap260 ns安全参数起步，PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、Imax/Ipre1.0 A、Iterm0.5 A和AI OFF不变。

本点采集45 s，要求`PRECHARGE→CC`并稳定保持CC、末10 s外部Iout在0.9787~1.0817 A、fault0并最终回到READY。原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN20_TPS_100_564_CAP260_1A_v47m_r14.log`；若不通过，锁定20 V/CV43 V只改一个参数修复，不进入18 V。

### CHG-VIN-05 LFP CC 固定 CV43：20 V PASS

`CHG_VIN_05_LFP_CC_CV43_V20_01`按TPS100/564 ns、cap260 ns完成45 s实测。42个RK8510低频只读样本峰值1.077 A/46.294 W；末10点均值42.9850 V/1.0438 A/44.8674 W，电流范围1.041~1.047 A，落在0.9787~1.0817 A允许区间内。

板端末20个运行窗口均值Vin=19.059 V、Vout=42.717 V、Iout=0.997 A、Pin/Pout=47.685/42.590 W；最高温度30.7/30.9 ℃。状态`PRECHARGE→CC`并保持CC，requested/effective=TPS/TPS共113条，末段raw phase 20~24 ns、effective phase 252~256 ns，fault非零记录0，最终`READY/PWM0/RUN0`。结论`PASS_LFP_CC_CV43_VIN20_IN_BAND`，允许进入18 V点。

### CHG-VIN-05 LFP CC 固定 CV43：18 V PRETEST

测试ID`CHG_VIN_05_LFP_CC_CV43_V18_01`。操作者只把输入电源从20.0 V改为18.0 V，限流保持5.0 A、输出保持ON；RK8510继续保持CV43.0 V、LOAD ON且主机不写入。板端直接采用同一18 V/CV44 V最终通过的TPS100/564 ns、cap300 ns，不再重试已经失败的268/276/284/292 ns；PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、Imax/Ipre1.0 A、Iterm0.5 A和AI OFF不变。

本点采集45 s，要求`PRECHARGE→CC`并稳定保持CC、末10 s外部Iout在0.9787~1.0817 A、fault0并最终回到READY。原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN18_TPS_100_564_CAP300_1A_v47m_r15.log`；若不通过，锁定18 V/CV43 V逐项修复，不进入返回段。

### CHG-VIN-05 LFP CC 固定 CV43：18 V 首轮 FAIL并锁点

`CHG_VIN_05_LFP_CC_CV43_V18_01`按历史通过的TPS100/564 ns、cap300 ns运行45 s，但没有达到固定CV下的1 A目标。42个RK8510低频只读样本峰值仅0.256 A/11.003 W；末10点电流序列为0.003/0.003/0.003/0.003/0.248/0.250/0.252/0.253/0.255/0.256 A，均值0.1526 A，明显低于允许下限0.9787 A。

板端共记录97条SOFTSTART和33条RUN；requested/effective=TPS/TPS共121条，其中15条已经达到effective phase=300 ns上限。末段板端约Vin17.23 V、Vout42.77 V、Iout0.22 A，说明状态机已经给出`Icmd/Iactive/Icpu1=1.00 A`，但TPS传输能力受cap300 ns限制。全程fault0、最高温度30.7/30.8 ℃并最终回到READY，因此不是硬件故障或过热，而是当前参数不能在18 V/CV43 V提供1 A。结论`FAIL_LFP_CC_CV43_VIN18_PHASE_CAP300_INSUFFICIENT`，18 V点继续锁定。

修复轮`CHG_VIN_05_LFP_CC_CV43_V18_01_FIX1`只把phase cap从300→308 ns，保持TPS100/564 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、1 A目标、AI OFF和全部物理工况不变。8 ns是此前18 V调参采用的最小步长，且仍有100 W功率上限；采集延长到75 s，以覆盖v47m约30 s低输入软启动并获得足够RUN稳定窗口。日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN18_TPS_100_564_CAP308_1A_v47m_r16.log`。

### 18 V FIX1 执行前计划更新：采用常规上限320 ns

操作者允许在足够安全的前提下增大裕量。固件静态边界确认`DAB_PHASE_NORMAL_MAX_COUNTS=80`，每计数4 ns，即常规闭环上限320 ns；只有超过320 ns才需要`unlock 400`进入最高480 ns扩展区。为节省调参轮次但不进入扩展区，未执行的308 ns预案由320 ns替代；这不是直接固定相移，电流PI仍只按1 A目标取所需相移，参考斜率仍为0.05 A/s、功率上限100 W、v47m相移上升速率仍为0.04 ns/ms。

FIX1除phase cap从300→320 ns外不改任何参数，采集75 s。新日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN18_TPS_100_564_CAP320_1A_v47m_r16.log`；原cap308日志未生成并保留PRETEST审计记录。若320 ns仍不能达到0.9787 A下限，不进入超过320 ns扩展区盲目加相移，而是锁点分析TPS分区。

### 18 V FIX1 cap320 仍FAIL，停止增加cap

cap320修复轮完整运行75 s。69个外部只读样本峰值0.700 A/30.091 W；末10点稳定为42.9860 V/0.6969 A/29.9564 W，范围0.694~0.700 A，低于允许下限0.9787 A。板端末20个RUN窗口均值Vin=17.188 V、Vout=42.792 V、Iout=0.661 A、Pin/Pout=38.015/28.275 W；最高温度32.3/31.9 ℃，fault0并回到READY。

TPS100/564 ns内相移差对应232 ns有效相移占用，v47m在cap320下只允许raw phase=88 ns，末段持续`effective_phase=320 ns`饱和。对照2026-08-17 v44a历史点，旧版本在板端Vin18.003 V时记录`phase_cmd_avg=266 ns`、Iout0.999 A，而v47m的cap语义已改为限制含内相移的有效相移；因此不能照搬旧cap268数值。结论`FAIL_LFP_CC_CV43_VIN18_TPS_NORMAL_CAP320_SATURATION`，禁止继续超过320 ns进入扩展区。

### 18 V FIX2 PRETEST：只切换到SPS

精确18 V/CV43 V工况继续锁定。FIX2保持cap320 ns、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、Imax/Ipre1 A、Iterm0.5 A与AI OFF不变，只把调制机制TPS切为SPS，SPS按定义不使用内相移。历史`G2_SPS_CV36_I2`已在接近19 V输入、约72 W输出下通过2 A闭环，而本点目标功率仅约43 W，因此该迁移比进入大于320 ns扩展相移区更安全。

FIX2采集75 s，日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN18_SPS_CAP320_1A_v47m_r17.log`。必须末10点进入0.9787~1.0817 A、保持CC、fault0并回到READY才解除18 V锁点。

### 18 V FIX2 SPS 启动失败，继续锁点

SPS修复轮完整运行75 s，69个外部样本峰值仅0.004 A/0.142 W，末10点稳定为35.0756 V/0.0030 A。板端188条SOFTSTART、0条RUN，requested/effective=SPS/SPS且raw/effective phase始终4/4 ns；末20个软启动窗口均值Vin=17.258 V、Vout=34.872 V、Iout=-0.026 A。最高温度31.5/32.5 ℃、fault0并回到READY。

结论`FAIL_LFP_CC_CV43_VIN18_SPS_SOFTSTART_4NS_NO_RUN`：v47m对SPS保留4 ns软启动相移，18 V下无法达到约39.6 V的RUN门槛。历史SPS功率点不能直接迁移到当前v47m低输入启动；18 V必须依靠EPS/TPS内相移缓慢释放来引导启动。

### 18 V FIX3 PRETEST：TPS次级内相移564→536 ns

回到可完成低输入启动的TPS，并保持primary inner100 ns、cap320 ns、1 A目标、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V与AI OFF全部不变，只把secondary inner从564→536 ns。根据本轮cap300与cap320稳态电流差，20 ns有效上限增加带来约0.44 A外部电流增量；当前0.70 A到1 A约需14 ns额外raw outer authority。次级内相移减少28 ns按`raw=effective+primary/2-secondary/2`恰好释放14 ns，同时启动物理内相移地板仅从232→218 ns，仍接近已经通过启动的分区。

FIX3运行75 s，原始日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-19_CHG_VIN05_LFP_CC_CV43_VIN18_TPS_100_536_CAP320_1A_v47m_r18.log`。若电流进入允许带则固定此18 V参数；若仍不通过，继续只微调secondary inner，不跨越cap320。

### 18 V FIX3 冷启动失败，确认需要两阶段分区

TPS100/536 ns从冷态运行75 s，69个外部样本末10点为38.8267 V/0.004 A，板端197条SOFTSTART、0条RUN。末20个窗口均值Vin=17.060 V、Vout=38.325 V、Iout=-0.023 A；full inner到位后的有效相移地板为218 ns、raw=0 ns，仍低于约39.6 V的RUN门槛。最高温度31.5/32.4 ℃、fault0并回到READY。结论`FAIL_LFP_CC_CV43_VIN18_TPS_100_536_STARTUP_BELOW_RUN`。

该结果与TPS100/564可启动且稳态cap320只有0.70 A共同证明：18 V需要100/564 ns提供232 ns冷启动地板，但进入RUN后需要降低secondary inner以释放外相移控制余量。因此下一轮不能再用单一分区从冷态启动，而应执行`100/564启动→RUN后100/536稳态`的两阶段过渡。

### 18 V FIX4 PRETEST：RUN后无扰切换100/564→100/536

现有`tools/Run-DabCurrentTest.ps1`增加了默认关闭的可选参数`RunTransitionPrimaryInnerNs/RunTransitionSecondaryInnerNs/RunTransitionDelayMs`：只有显式设置时，脚本检测首条RUN遥测，等待指定时间后向板端发送一次`inner`命令；不读写电子负载，也不改变原有默认行为。PowerShell解析错误0，脚本SHA256为`98010903901AB3CB871EC86BC0007D8AB93FD13562866E98D3D0286C6760CD55`。

`CHG_VIN_05_LFP_CC_CV43_V18_01_FIX4`先以TPS100/564 ns、cap320完成冷启动，检测到RUN后500 ms只把secondary inner切到536 ns；1 A目标、PI2.0/0.2、slew0.05 A/s、P100 W、Vref44 V、AI OFF及物理工况全部不变。采集95 s以覆盖约40 s启动、过渡和至少30 s稳态，日志预留为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_CHG_VIN05_LFP_CC_CV43_VIN18_TPS_START100_564_RUN100_536_CAP320_1A_v47m_r19.log`。验收除末10点0.9787~1.0817 A外，还要求过渡期间不掉RUN、fault0并最终回到READY。

### FIX4 作废：运行时调度必须在下位机完成

操作者明确现场上位机只能下发电池类型、目标电流/电压和启停等高层目标，调制选择、启动分区、RUN 后过渡及达到目标的闭环行为必须由下位机独立完成。因此主机检测 RUN 后发送 `inner 100 536` 的方案违反最终系统架构，立即停止并撤销脚本功能；`tools/Run-DabCurrentTest.ps1`已恢复到解析错误0、SHA256=`15A320DA8833B77FA3171BA2E2FDE75B5977F4B4A83540CCE001CF99A499BA15`。

r19 在中止前记录23个电子负载低频只读样本和77条板端SOFTSTART，0条RUN；外部末10点37.0981 V/0.0030 A，板端末20条约Vin17.065 V、Vout36.605 V、Iout-0.028 A。日志中`TX inner 100 536`与`RUN_TRANSITION`均为0，说明不合规的运行时切换未实际发生。Ctrl+C后显式向COM4发送`off`并确认READY/PWM0/RUN0/fault0；电子负载未被写入。结论`INVALID_ABORTED_HOST_ORCHESTRATION_NOT_ALLOWED`，不可复用。

审计本轮固定CV43宽输入组：24/22/20 V三个PASS和18 V四个已完成FAIL均在启动前由主机发送了`sched off`、手动`mod/inner/cap/PI`等底层配置，另有上述r19中止点，共8次不能作为下位机自主功能验收。前7次仍保留为参数标定证据；固件必须载入通过数据和确定性选择逻辑后，再以串口只下发`bat profile`、充电目标及`bat start/stop`重新验收。

### v48 下位机自主充电固化、构建与双核升级

CPU0 固件更新为`2026-08-20-dualcore-autonomous-charge-v48`。参数生成器新增读取本轮 MOS 回归 CSV 中的 powered PASS 记录，固件充电表由14条增至25条，版本`v48-autonomous-charge-powered-seeds`；24 V EPS240/0 cap120、22 V EPS0/44 cap120、20 V TPS100/564 cap260以及此前CV→DONE成功束均已进入正式生成表。FAIL/INVALID和`reuse_allowed=false`记录不提升。18 V/CV43 V仍没有PASS，因此只以ID `48018001`加入严格限于LFP/CC/约1 A/低输入的待验证修复候选，不伪装为生成PASS条目。

`bat start`现在从板端ADC、profile、阶段和用户目标自主选束并直接启动功率级；`bat stop`直接停止PWM及四路功率路径。参数表不再覆盖上位机给定的Imax/Iterm。运行中固件每250 ms本地评估一次，候选持续1 s才切换；18 V修复由板端保持TPS100/564 ns完成冷启动，RUN后500 ms自主把目标缓慢变为100/536 ns。输入变化时由本地表重新选择24/22/20 V束；两个AI模型均不是执行条件。

上位机串口智能充电计划同步删除`mod/p/vinset/vref/energy/arch/sched/on`，配置阶段只发送profile、目标CV和最大电流，启停分别只发送`bat start`/`bat stop`。Python3.13上22项协议/传输测试全部PASS；Python3.9首次运行因项目原有`dataclass(slots=True)`版本要求未进入测试，原错误保留。

生成器`--check`通过，CPU0 text/data/bss=`177118/560/172405` bytes，ELF SHA256=`577C4A725E6A0618388F3E6B79D77FBD86DCD21D05BF281168057E92687B0229`，SREC SHA256=`EF0B707C8ED6EF9943060B1D6CA4FDD3D21CD719111064E35566F21BE41E0581`。CPU1 energy-v46 text/data/bss=`17198/132/9104` bytes，ELF SHA256=`7CBA199B067E665F4FD70999FDC7A169843FDA6E26AEC4E15E0258F0722D9E08`，SREC SHA256=`2690D4039B613559DA826F1CC7CB2C7C2C85F12B37A5785D73B82BB24F1587CD`。

烧录前COM4只读确认v47m处于READY/PWM0/RUN0/fault0、路径全关、ADC valid。J-Link双核脚本退出码0：CPU0 Bank0擦写/编程/校验178176 bytes，CPU1内容一致；原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48_autonomous_charge_dualcore_flash.log`。烧录后只读确认v48版本、READY/PWM0/RUN0/fault0、路径全关、ADC invalid=0x0000，并从板端读回25条充电表版本。构建/烧录本身不作为物理功能PASS。

创建锁点复测`CHG_VIN_05_LFP_CC_CV43_V18_01_FIX5_AUTONOMOUS_V48`。保持输入18.0 V、RK8510 CV43.0 V/LOAD ON、Imax/Ipre1 A、Iterm0.5 A、AI OFF。UART只允许`ai off`、`bat stop`、profile/目标、日志显示和`bat start`；禁止主机发送mod/inner/cap/PI/arch/sched/p/vinset/on或依据RUN补发任何命令。运行95 s，必须末10点0.9787~1.0817 A、CC、fault0并由`bat stop`回到READY才通过。

### v48 自主复测 r20：配置阶段失败，原工况锁定

`CHG_VIN_05_LFP_CC_CV43_V18_01_FIX5_AUTONOMOUS_V48`仅发送高层命令，未发送任何mod/inner/cap/PI/arch/sched/p/vinset/on或RUN触发命令。但设置`bat profile lfp12`后再设置`bat vcv 43.8`，共享能量控制层错误地把profile改成`CUSTOM`；`bat start`因此报告`[DAB][CHGSCHED][NO_MATCH] profile=CUSTOM Vin=17.42 Vcv=43.80 Imax=1.00 has no bounded powered seed`并保持功率启动未武装。19个RK8510低频只读样本均为0 V/0 A，板端PWM从未启动、fault0。随后只发送高层`bat stop`和日志命令，确认READY/PWM0/RUN0、四路路径关闭；电子负载未被写入。

结论`FAIL_AUTONOMOUS_TARGET_OVERRIDE_CHANGED_PROFILE_TO_CUSTOM_NO_POWER`。这不是物理功率参数失败，而是板端目标配置语义缺陷；按锁点约束不进入下一工况。修复放在CPU0/CPU1共用的`shared/dab_energy_control.c`：预充/CV/浮充目标只覆盖所选电池类型的目标值，不再擦除LFP12或LIION10S类型；新增主机回归测试同时覆盖电压和电流目标后profile仍为LFP12。CPU0版本升为`2026-08-20-dualcore-autonomous-charge-v48a`，原25条充电参数表及全部已通过种子保持不变，修复后仍在18 V/CV43 V原工况重新测试。

### v48a 双核升级与 r21 原工况 PRETEST

生成器`--check`确认charger/current/voltage表分别为25/13/2条。CPU0 v48a text/data/bss=`177102/560/172405` bytes，ELF/SREC SHA256=`15566157C4FD187E8262DCA71EE92857E98687ECE6F534043D9CFF5ECA1DDFF0`/`981E14CC42B1ABE85BBC86346A385255C81532DF0E8BCAD3B1748410C32F59B6`；CPU1共享修复后text/data/bss=`17190/132/9104` bytes，ELF/SREC SHA256=`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。烧录前只读确认READY/PWM0/RUN0/fault0及四路路径关闭；J-Link退出码0，CPU0 178176 bytes及CPU1 18432 bytes均完成Erase/Program/Verify。

烧录后串口高层配置回归确认：依次设置LFP12、capacity10 Ah、Vcv43.8 V、Imax/Ipre1 A、Iterm0.5 A后，板端仍报告`profile=LIFEPO4_12S_43P8V`而不是CUSTOM；`chgtable show`读回`v48-autonomous-charge-powered-seeds revision=2 entries=25`。此处仅证明配置语义、表载入和无功率状态，不作为充电功能物理PASS。

原工况复测ID为`CHG_VIN_05_LFP_CC_CV43_V18_01_FIX6_AUTONOMOUS_V48A`。输入仍为18.0 V，电子负载仍为CV43.0 V/LOAD ON，无需操作者换工况。板端只接收`ai off`、高层`bat stop/profile/capacity/vcv/imax/ipre/iterm/start`以及日志命令；测试结束只发送`bat stop/stream off/show`。禁止任何mod/inner/cap/PI/arch/sched/p/vinset/on或RUN触发命令。采集95 s，电子负载只以不快于1 Hz的短连接查询读取；必须末10点0.9787~1.0817 A、保持CC、fault0并最终READY/PWM0/RUN0才通过。

### r21 物理 FAIL与智能充电范围改为20~24 V

r21使用纯高层串口命令后，板端自主选择候选48018001、完成TPS冷启动并进入RUN，也在本地把实际分区过渡到100/536 ns；上位机发送0条运行时底层命令。80个电子负载只读样本中，末10个有效稳态点为42.9836 V/0.3886 A/16.7027 W，电流范围0.387~0.390 A，低于0.9787 A下限。板端末20个RUN窗口均值Vin=17.1895 V、Vout=42.722 V、Iout=0.3585 A、Pin/Pout=24.575/15.255 W；相移持续约raw104 ns/effective322 ns饱和，最高温度31.7/31.2 ℃，fault非零0，最终高层`bat stop`确认READY/PWM0/RUN0、四路路径关闭。结论`FAIL_LFP_CC_CV43_VIN18_AUTONOMOUS_CAP320_INSUFFICIENT`，原始数据保留且不可提升为PASS参数。

操作者随后明确：智能充电的全部功能不考虑20 V以下工况。于是停止18 V继续调参，不制作v48b，也不把该失败伪装为通过；智能充电交付范围统一改为Vin=20/22/24 V及24→22→20→22→24往返。操作者继续明确MPPT同样只覆盖20~24 V，因此MPPT扫描下限和PV工况同步改为20 V；CP及阶段四精准电流范围暂不改变。下一点回到已经具有历史PASS种子的20 V/CV43 V，并仍以v48a仅下发高层目标复测。

### 20 V/CV43 V自主验收 r22 PRETEST

操作者完成输入设置并留出稳定时间后，板端只读实测Vin=19.39 V，对应电源面板20.0 V；READY/PWM0/RUN0/fault0且四路路径关闭。RK8510以短连接低频查询确认IDN正常、mode=2、CV=43 V、state=1，主机未写入电子负载。

测试ID`CHG_VIN_05_LFP_CC_CV43_V20_AUTONOMOUS_V48A_r22`。v48a的25条充电表中已经包含历史PASS记录571992544：20 V/CV43 V、TPS100/564 ns、cap260 ns、PI2.0/0.2、slew0.05 A/s、外部稳态1.0438 A。测试只发送AI OFF、LFP12、容量和充电目标、`bat start/stop`及日志命令；禁止主机发送任何调制、inner、cap、PI、arch、sched、p、vinset、on或RUN触发命令。运行75 s，必须板端自主选中20 V种子，末10个外部点在0.9787~1.0817 A、保持CC、fault0并最终READY/PWM0/RUN0。

### r22 20 V/CV43 V自主 PASS

板端仅凭高层目标自主选中`entry=571992544`，请求/实际调制为TPS/TPS，最终分区100/564 ns、raw相移主要20 ns、effective约252 ns，cap260 ns未持续饱和。63个电子负载只读样本峰值1.062 A/45.649 W；末10点均值42.9863 V/1.0478 A/45.0407 W，电流范围1.045~1.050 A，满足0.9787~1.0817 A验收带。

板端末20个RUN窗口均值Vin=19.0830 V、Vout=42.7195 V、Iout=0.9990 A、Pin/Pout=47.670/42.680 W；最高温度31.1/30.9 ℃，fault非零0。结束只发送高层`bat stop`及日志命令，确认READY/PWM0/RUN0和路径全关。结论`PASS_LFP_CC_CV43_VIN20_AUTONOMOUS_V48A`，该点证明历史成功参数已由正式固件自主载入并在AI OFF下复现，可进入22 V点。

### 22 V/CV43 V自主验收 r23 PRETEST

操作者完成22.0 V/5.0 A/ON输入设置后，主机留出完整60 s且不访问COM4/COM24。随后板端只读实测Vin=21.38 V、READY/PWM0/RUN0/fault0；RK8510短连接确认mode=2、CV=43 V、state=1，未写入。

测试ID`CHG_VIN_05_LFP_CC_CV43_V22_AUTONOMOUS_V48A_r23`。历史PASS种子136393323为22 V/CV43 V、EPS0/44 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s、外部稳态1.0357 A。仅发送AI OFF、LFP12、容量和充电目标、`bat start/stop`及日志命令；禁止任何主机底层控制或运行时编排。运行60 s，要求板端自主选择该种子，末10外部点0.9787~1.0817 A、CC、fault0、最终READY/PWM0/RUN0。

### r23 22 V/CV43 V自主 PASS

板端仅凭高层充电目标自主选中`entry=136393323`，请求/实际调制为EPS/EPS，分区0/44 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s均由正式参数表载入。电流在板端斜坡约20 s后进入目标带，主机没有发送调制、inner、cap、PI、arch、sched、p、vinset、on或任何RUN触发命令。

51个电子负载低频只读样本中45个为带载有效样本；末10个有效稳态点为42.9846 V/1.0377 A/44.6047 W，电流范围1.035~1.040 A，满足0.9787~1.0817 A验收带，峰值1.040 A/44.705 W。停止后的低电流样本按有效带载时间窗排除。

板端末20个RUN窗口均值Vin=21.0820 V、Vout=42.7290 V、Iin=2.2115 A、Iout=0.9995 A、Pin/Pout=46.635/42.680 W；最高温度30.6/30.4 ℃，fault非零0。结束只发送高层`bat stop`及日志命令，确认READY/PWM0/RUN0和功率路径关闭。结论`PASS_LFP_CC_CV43_VIN22_AUTONOMOUS_V48A`，该点可复用，按计划进入24 V点。

### 24 V/CV43 V自主验收 r24 PRETEST

操作者完成24.0 V/5.0 A/ON输入设置，电子负载保持CV43.0 V/LOAD ON。该点在操作者将换工况等待要求改为10 s之前已经完成60 s无仪器访问稳定时间；后续换工况统一只等待10 s。随后板端只读实测Vin=23.31 V、READY/PWM0/RUN0/fault0且功率路径全关；RK8510短连接只读确认mode=2、CV=43 V、state=1，未写入或切换电子负载。

测试ID`CHG_VIN_05_LFP_CC_CV43_V24_AUTONOMOUS_V48A_r24`。正式表内历史PASS种子`entry=183355872`为24 V/CV43 V、EPS240/0 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s、历史外部稳态1.0302 A。仅发送AI OFF、LFP12、容量和充电目标、`bat start/stop`及日志命令；禁止任何主机底层控制或RUN时编排。运行60 s，要求板端自主选择24 V种子，末10外部点0.9787~1.0817 A、CC、fault0、最终READY/PWM0/RUN0。

### r24 24 V/CV43 V自主 PASS

板端启动时先选中同电压/同CV的已验证候选`981927885`，持续本地确认1 s后自主切换到精确历史种子`183355872`；两者均为EPS240/0 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s。切换全程保持RUN，主机底层控制命令计数为0，AI OFF且CC阶段持续有效。

47个电子负载低频只读样本中42个为带载有效样本；末10个有效稳态点为42.9859 V/1.0264 A/44.1201 W，电流范围1.024~1.028 A，满足0.9787~1.0817 A验收带。启动阶段出现单个外部采样峰值45.843 V/1.863 A/85.405 W，随后立即回落并进入板端0.05 A/s目标斜坡；该瞬态原始记录保留，不纳入末段稳态判定，也不隐藏。

板端末20个RUN窗口均值Vin=22.9320 V、Vout=42.6045 V、Iin=2.1065 A、Iout=1.0005 A、Pin/Pout=48.280/42.640 W；最高温度31.5/30.2 ℃，fault非零0。结束仅发送高层`bat stop`及日志命令，最终READY/PWM0/RUN0、功率路径关闭。结论`PASS_LFP_CC_CV43_VIN24_AUTONOMOUS_V48A`。至此固定CV43条件下20/22/24 V三个自主点全部通过，外部末10均值分别1.0478/1.0377/1.0264 A，均在统一验收带内。

操作者随后将所有人工换工况稳定窗口从60 s明确缩短为10 s。计划中的通用人工操作、智能充电宽输入和最终展示三处约束已同步更新；后续收到“完成”后先保持COM4/COM24均不访问10 s，再执行预检。采集与稳态判定时长不因该操作窗口缩短而自动减少。

### LFP 24 V/CV44 V自主CV→DONE r25 PRETEST

操作者把RK8510手动改为CV44.0 V并保持LOAD ON，输入保持24.0 V/5.0 A/ON。收到“完成”后主机按新要求保持COM4/COM24均不访问10 s；随后只读预检确认板端Vin=23.32 V、READY/PWM0/RUN0/fault0、功率路径全关，RK8510 mode=2、CV=44 V、state=1，未写入或切换电子负载。

测试ID`CHG_VIN_06_LFP_CV44_DONE_V24_AUTONOMOUS_V48A_r25`，沿用原锁定失败工况LFP12、Vcv43.8 V、Imax/Ipre1 A、Iterm0.5 A及AI OFF。正式充电表中24 V/CV44 V已有CV束`99799358`和此前完成CV→DONE的种子`107349017`，均为EPS240/0 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s；后者历史外部稳态0.2589 A。主机只发送profile/充电目标及`bat start/stop`，禁止任何调制或运行时底层编排。

采集65 s。通过条件：阶段必须进入CV，外部电流随接近Vcv降至Iterm以下并连续保持至少30 s，随后板端自主进入DONE；不得回升到CC目标、反向或出现fault，结束确认READY/PWM0/RUN0。未进入DONE或低流保持中断即按原CV44 V工况锁定修复。

### r25 LFP 24 V/CV44 V自主CV→DONE PASS

板端仅凭高层充电目标依次经过CC、CV和DONE；对应遥测记录数32/150/123。CV阶段本地选择`entry=99799358`，进入DONE后本地确定性回退仍保持EPS240/0 ns；主机底层控制命令计数为0，AI OFF。原失败中“低于Iterm约15 s后回升”的现象未复现。

RK8510启动早期有数个47~49 V/0 A回读，与同期板端43.7 V带载遥测不一致，原始值完整保留；进入有效CV负载窗口后，外部电流从0.432 A降至0.243~0.244 A。0.5 A以下连续有效窗口从elapsed=9.330 s保持到62.957 s，共53.627 s，超过30 s终止条件；末10有效点43.9901 V/0.2439 A/10.7288 W，未回升。

板端末20个RUN窗口均值Vin=23.1505 V、Vout=43.7600 V、Iout=0.2165 A、Pin/Pout=13.585/9.485 W；最高温度31.2/30.4 ℃，fault非零0。结束只发送高层`bat stop`，确认READY/PWM0/RUN0及路径全关。结论`PASS_LFP_CV44_TAPER_DONE_V24_AUTONOMOUS_V48A`，原CV44 V锁点已修复通过；下一步在负载CV44保持不变时验证Vin=22/20 V电流不变性，任一点超出24 V基准0.2439 A的`max(0.05 A,5%)=0.05 A`即锁点修复。

### LFP 22 V/CV44 V自主CV/DONE不变性 r26 PRETEST

操作者仅把输入改为22.0 V/5.0 A/ON，RK8510保持CV44.0 V/LOAD ON。收到“完成”后COM4/COM24均停止访问10 s；预检实测板端Vin=21.37 V、READY/PWM0/RUN0/fault0且路径全关，RK8510只读为mode=2、CV=44 V、state=1。

测试ID`CHG_VIN_06_LFP_CV44_DONE_V22_AUTONOMOUS_V48A_r26`。LFP12、Vcv43.8 V、Imax/Ipre1 A、Iterm0.5 A和AI OFF与24 V基准完全相同，只允许固件按Vin自主选择EPS参数束。24 V外部基准0.2439 A，因此22 V稳态允许带为0.1939~0.2939 A；同时仍须完成CV→DONE、fault0并最终READY。正式表中22 V/CV44 V历史束曾记录0.3734~0.4927 A，只作为候选证据，不预先写成通过。若本轮超出允许带，即在22 V/CV44 V锁点修复，不进入20 V。

### r26 22 V/CV44 V电流不变性 FAIL，原工况锁定

固件AI OFF下自主选中22 V/CV44 V的EPS0/44 ns束`223218701`，状态机仍完成CC→CV→DONE；主机底层命令计数0。43个RK8510只读样本中38个为有效带载点，末10点为43.9918 V/0.3754 A/16.514 W，范围0.374~0.376 A。相对24 V基准0.2439 A高0.1315 A，明显超过`max(0.05 A,5%)=0.05 A`，结论`FAIL_LFP_CV44_VIN22_CURRENT_INVARIANCE_HIGH_BY_0P1315A`。

板端末20个RUN窗口均值Vin=21.2530 V、Vout=43.9025 V、Iout=0.3370 A、Pin/Pout=19.170/14.820 W；最高温度30.5/30.9 ℃、fault非零0，结束高层停止后READY/PWM0/RUN0。失败原因指向CV/DONE时EPS0/44 ns的22 ns有效内相移地板：outer raw已为0且持续饱和，仍比24 V EPS240/0 ns的零有效地板传递更多功率。保持物理工况22 V/CV44 V不变，下一步把消除该地板和跨Vin保持目标电流的逻辑放入下位机，构建/双核升级后原点复测；不进入20 V。

### r26 锁点修复：v48b 构建、升级前确认与 r27 PRETEST

根因确认：DAB有效相移按`raw + secondary_inner/2 - primary_inner/2`形成，22 V历史EPS0/44 ns束即使outer raw已经归零仍存在22 ns有效相移，因此CV/DONE阶段无法继续降低电流；同时DONE阶段仍被调度器归一成CC束，功率级没有由下位机自主关闭。v48b在下位机内完成两项修复：LFP/Li-ion进入CV及DONE时自主把inner目标归零并由电流外环控制有效相移；DONE由本地完成服务直接关闭PWM和四路功率路径、取消本次调度并回到READY。上位机仍只给高层电池目标，现场不依赖调参脚本；AI OFF路径保持完整。

正式参数表已把r22/r23/r24/r25四个新PASS记录提升进固件，charger/current/voltage条目为29/13/2，生成器`--check`通过；r26 FAIL未进入表。CPU0 v48b完整构建通过，text/data/bss=`177830/560/172405` bytes，ELF/SREC SHA256=`3D4FB36E34454263ECD9D9288B3CCC5714F1907A7FFC356D59215F3B435F7373`/`4FA371A0C22851A7702341E0B9C3CE7BB37A3984A34A462BE36BE0FD8B95A25F`。CPU1依赖构建命令退出码0，text/data/bss=`17190/132/9104` bytes；源和产物未变化，ELF/SREC SHA256仍为`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。上位机Python3.13协议与传输回归22/22 PASS；另有参数策略C文件ARM语法检查通过，但本机Renesas clang不具备WASM主机执行后端，因此不把该语法检查记成C单元测试PASS。

升级前仅查询COM4的`show/adcdiag/version`：旧v48a处于`READY/CTRL=OFF/PWM=0/RUN0/fault=0`，四路路径全关，ADC valid且invalid=`0x0000`，板端Vin=21.37 V；未访问COM24，更未写入或切换电子负载。计划用固化的双核脚本烧录当前ELF，原始J-Link日志保存为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48b_charge_done_stop_dualcore_flash.log`。烧录和启动确认本身不记作物理功能PASS。

r27继续锁定同一人工工况：输入22.0 V/5.0 A/ON，RK8510保持CV44.0 V/LOAD ON，不要求操作者再次换工况。测试ID`CHG_VIN_06_LFP_CV44_DONE_V22_AUTONOMOUS_V48B_r27`；LFP12、Vcv43.8 V、Imax/Ipre1 A、Iterm0.5 A、AI OFF均不变。只发送高层充电目标和`bat start`，预期CV时inner自主归零、继续稳定减小电流，达到DONE后板端自行回到READY/PWM0/RUN0，无需上位机`bat stop`。活动窗口电流仍按24 V基准0.2439 A的0.1939~0.2939 A判定；若仍越界或DONE后不自主停止，继续留在22 V/CV44 V修复，不进入下一点。

J-Link双核脚本退出码0：CPU0 Bank0完成180224 bytes擦除、编程和校验，Bank1内容一致；CPU1内容一致而跳过写入。原始日志已实时保存到计划路径。烧录后COM4只读确认版本为`2026-08-20-dualcore-charge-done-stop-v48b`，`READY/CTRL=OFF/PWM0/RUN0/fault0`、四路路径全关、ADC valid/invalid=`0x0000`、Vin=21.38 V，并读回正式充电表29条。电子负载仍未访问。现在允许进入r27物理复测，但升级成功本身仍不算功能PASS。

### r27 22 V/CV44 V内相移地板修复与自主DONE PASS

v48b只接收AI OFF和LFP12/容量/Vcv/Imax/Ipre/Iterm/`bat start`高层命令；运行时底层命令计数0。板端进入CV后把EPS inner从历史束0/44 ns自主斜降到0/0 ns，有效相移由原先不可消除的22 ns地板降到约12~16 ns，外部CV保持窗口末10点为43.9932 V/0.0191 A/0.8394 W，电流范围0.016~0.021 A。约36 s时板端出现`[BAT][AUTO][DONE]`，自行关闭PWM和四路功率路径并回到READY；主机未在结束阶段发送`bat stop`，输出随后自然降为0 V/0 A。

板端169个RUN记录末20点均值Vin=21.2205 V、Vout=43.7180 V、Iout=0.0060 A、Pin/Pout=3.345/0.295 W；最高温度29.1/29.9 ℃，fault非零0。结论`PASS_LFP_CV44_TAPER_DONE_AUTOSTOP_V22_V48B`：r26的22 ns功率地板和DONE继续带载两项缺陷均已在相同22 V/CV44 V工况修复。旧v48a的24 V 0.2439 A“基准”来自同类残余调制地板，不能再作为CV44终止电流目标；正确目标是Vout高于43.8 V时电流降至近零并停止。该次仅作为逻辑修复证据，不提升为新的功率种子；固定CV时的Vin不变性仍由已通过的CV43 V、20/22/24 V三点1.0478/1.0377/1.0264 A证明，后续展示也使用尚未进入DONE的CV43 V工作区。

### Li-ion 10S 24 V/CV40 V自主CC基准 r28 PRETEST

操作者把输入设为24.0 V/5.0 A/ON、RK8510设为CV40.0 V并保持LOAD ON。收到“完成”后COM4/COM24均不访问10 s；随后板端只读为v48b、Vin=23.34 V、READY/PWM0/RUN0/fault0、路径全关、ADC valid/invalid=`0x0000`，RK8510短连接只读为mode=2、CV=40 V、state=1，未写入或切换负载。

测试ID`CHG_VIN_02_LI_CC_CV40_V24_AUTONOMOUS_V48B_r28`。从正式表已有经验`liion10_v43_traj_40p0`开始：24 V/CV40 V、EPS600/0 ns、cap20 ns、current PI0.5/0.05、slew0.05 A/s，历史外部40.004 V/1.127 A/45.084 W。为符合智能充电不追求高电流精准度、从成功经验起步和节省时间三项要求，本轮高层目标设Li-ion10S、Vcv42.0 V、Imax/Ipre1.0 A、Iterm0.30 A；3 A不在智能充电阶段强行复测，留给阶段四精准电流。AI OFF，主机禁止发送mod/inner/cap/PI/arch/sched/p/vinset/on或RUN触发命令。

运行40 s并由高层`bat stop`结束。通过条件：板端自主选择已验证Li-ion束，完成启动并保持CC；末段外部电流先按0.8~1.3 A的充电规律带验收，fault0且最终READY/PWM0/RUN0。该点实际末10均值将作为后续22/20/22/24 V同CV不变性基准，后续每点必须满足`max(0.05 A,5%)`，失败即锁点修复。

### r28 稳态达到目标但启动过流，整体 FAIL并锁定原点

板端仅凭高层目标自主经历PRECHARGE→CC并从初始42 V候选切换到精确CV40种子`769155114`，最终EPS600/0 ns、cap20 ns、PI0.5/0.05、slew0.05 A/s。稳态本身通过：RK8510末10点39.9816 V/1.0495 A/41.9605 W，电流范围1.049~1.050 A；板端末20个RUN点Vin=23.0450 V、Vout=39.7495 V、Iout=1.0215 A、Pin/Pout=47.200/40.635 W，最高温度29.9/28.8 ℃，fault非零0，主机底层命令计数0，结束高层停止后READY/PWM0/RUN0、路径全关。

但是启动时正式束的primary inner从0 ns以约40 ns/s慢斜坡到600 ns，途中形成不合适的EPS工作区。外部有效峰值2.318 A/92.680 W，约9~17 s仍从2.318 A缓慢回落到1.433 A，显著超过高层`Imax=1.0 A`；启动早期另有47 V/0 A的RK8510与板端约40 V带载不一致回读，均原样保留。虽然没有fault或过温，也不能把末段稳态掩盖启动超限。结论`FAIL_LI_CC_CV40_STARTUP_OVERSHOOT_2P318A_EXCEEDS_IMAX1`，保持24 V/CV40 V原工况锁定；修复方向是在功率路径开启前把已验证inner束装载到PWM影子/当前目标，避免带载后从0 ns慢爬，随后构建、升级并在完全相同工况复测。

### r28 锁点修复：v48c功率启动前正式束预装

v48c把修复完整放入下位机：仅当高层`bat start`已经由正式表选中EPS/TPS束、调度和energy supervisor均有效且当前不是CV/DONE零inner阶段时，先在PWM输出仍关闭的状态下把已验证inner写入影子/当前寄存器，并计算使有效相移为0的raw补偿；成功后才允许gate enable。预装失败则立即保持所有功率路径关闭并回到READY。普通手动、SPS、CV/DONE零inner路径不冒充预装，运行时模式切换仍走原无扰斜坡。版本为`2026-08-20-dualcore-charge-prearm-v48c`。

CPU0 ARM完整构建退出码0，text/data/bss=`178742/560/172405` bytes，ELF/SREC SHA256=`C90F70292A4A3A70938DD29CCF2197CECEEF979DB54531E6076F7D295295136A`/`82A1A6575305096FC6519D33A993A57D92DC308B8D075229E25549DA9B60456F`。CPU1依赖构建退出码0，源与产物未变，text/data/bss=`17190/132/9104` bytes，ELF/SREC SHA256=`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。正式参数生成器`--check`仍为29/13/2条且r28 FAIL未入表；Renesas clang参数策略语法检查通过，上位机Python3.13协议/传输回归22/22 PASS。

刷写前仅查询COM4：旧v48b仍为Vin=23.35 V、READY/PWM0/RUN0/fault0、路径全关、ADC valid/invalid=`0x0000`；正式Li-ion40 V束仍在RAM但功率停止。电子负载未访问。计划J-Link原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48c_charge_prearm_dualcore_flash.log`，烧录后仍须先证明无功率启动，再创建相同24 V/CV40 V r29 PRETEST。

J-Link双核脚本退出码0：CPU0 Bank0完成180224 bytes擦除、编程和校验，Bank1内容一致；CPU1内容一致。烧录后版本回读为v48c，板端Vin=23.34 V、READY/PWM0/RUN0/fault0、路径全关、ADC valid/invalid=`0x0000`，正式充电表29条；RK8510仍只读确认mode=2、CV40 V、state=1。升级本身不作为物理PASS。

r29测试ID`CHG_VIN_02_LI_CC_CV40_V24_AUTONOMOUS_V48C_r29`，人工工况和全部高层目标与r28完全相同，唯一变化是v48c在PWM OFF时预装正式束。必须在日志中看到`[DAB][CHGPREARM] entry=... PWM=0`先于PWM开启；外部全程有效带载峰值不得超过1.30 A，末10仍须0.8~1.3 A、CC、fault0并最终READY/PWM0/RUN0。若仍过冲，继续锁定本点修复。

### r29 Li-ion 24 V/CV40 V启动预装修复 PASS

v48c只接收AI OFF、Li-ion10S、Vcv42.0 V、Imax/Ipre1.0 A、Iterm0.30 A和`bat start/stop`高层命令，主机底层调制/内相移/启停命令计数0。下位机先选中`entry=777229444`，后在CC中自主切换为精确CV40种子`769155114`；最终均为EPS600/0 ns、cap20 ns、PI0.5/0.05、slew0.05 A/s。

首个被串口采集到的SOFTSTART帧已是PWM=1、raw=300 ns、primary/secondary inner=600/0 ns、effective phase=0 ns，证明已高于带载后从0 ns慢爬的r28状态。一次性`[DAB][CHGPREARM] ... PWM=0`文本本轮未被串口采集到，此漏采不隐藏；固件实际调用顺序为预装成功后才允许gate/PWM，预装失败则回READY且路径全关。因此以首帧寄存器状态和外部启动响应作为等价时序证据，不伪造缺失文本。

26个RK8510低频只读样本中21个为带载有效点；首个有效点elapsed=4.095 s即为39.985 V/1.069 A/42.743 W，也是全程最大电流/功率。相比r28的2.318 A/92.680 W，启动峰值降低1.249 A且低于1.30 A上限。末10个有效点为39.9854 V/1.0499 A/41.9804 W，电流范围1.049~1.051 A；作为后续22/20/22/24 V回程比较的24 V基准，统一允许带为0.9974~1.1024 A（`max(0.05 A,5%)=0.0525 A`）。

板端143个RUN记录末20点均值Vin=22.9790 V、Vout=39.6535 V、Iout=1.0240 A、Pin/Pout=48.320/40.620 W；最高温度29.5/28.2 ℃，fault非零0。结束只发高层`bat stop`，最终READY/PWM0/RUN0且四路功率路径全关。结论`PASS_LI_CC_CV40_STARTUP_PREARM_V24_AUTONOMOUS_V48C`，r28启动过流锁点已在原24 V/CV40 V工况修复，当前参数允许作为后续输入不变性比较基准；Li-ion整体尚未完成，暂不触发功能级最终提升/交付。

### Li-ion 22 V/CV40 V自主CC输入不变性 r30 PRETEST

操作者只把输入电源改为22.0 V/5.0 A/ON，RK8510保持CV40.0 V/LOAD ON。收到“完成”后COM4/COM24均不访问10 s；随后只读预检确认板端Vin=21.35 V、v48c、READY/PWM0/RUN0/fault0、四路功率路径全关、ADC valid/invalid=`0x0000`；RK8510为mode=2、CV=40 V、state=1，未写入或切换电子负载。预检原始文件为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_CHG_LI_CC_CV40_V22_AUTONOMOUS_V48C_r30_precheck.log`和对应`_load.csv`。

测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48C_r30`。高层目标与r29完全相同：AI OFF、Li-ion10S、Vcv42.0 V、Imax/Ipre1.0 A、Iterm0.30 A；主机不发送任何调制、inner、cap、PI、arch、sched、p、vinset或底层RUN触发命令。24 V基准为1.0499 A，因此22 V末10有效点必须在0.9974~1.1024 A内，同时全程有效峰值不超过1.30 A、状态保持CC、fault0并最终READY/PWM0/RUN0。任一失败即锁定22 V/CV40 V修复，不进入20 V。

### r30 Li-ion 22 V/CV40 V输入不变性 FAIL，锁定原点

v48c全程只接收与r29相同的高层命令，主机底层命令计数0。下位机在AI OFF下继续选用24 V/CV40种子`769155114`，EPS600/0 ns、cap20 ns、PI0.5/0.05、slew0.05 A/s；状态为PRECHARGE→CC，fault非零0。

26个RK8510低频只读样本中21个为带载有效点；外部最大0.088 A/3.537 W，末10为39.9824 V/0.0756 A/3.0220 W，范围0.075~0.076 A。该电流比24 V基准1.0499 A低0.9743 A，远低于允许下限0.9974 A，结论`FAIL_LI_CC_CV40_VIN22_CURRENT_LOW_0P0756A_PHASE_CAP_DIRECTION`。

板端143个RUN记录末20点均值Vin=21.0850 V、Vout=39.5895 V、Iout=0.0490 A、Pin/Pout=6.620/1.940 W；最高温度28.9/28.2 ℃，fault非零0。电流外环因正误差将raw从预装补偿点300 ns推到320 ns，effective phase持续+20 ns且`sat=1`，但输出功率反而降到约2 W；说明22 V下不能沿用24 V EPS600/0的单向正相移执行器。已有同输入、近似输出的LFP CV43成功经验为EPS0/44 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s；下一步将该低输入方向作为下位机自主候选而非主机脚本调参，构建/双核升级后在当前22 V/CV40 V原工况复测。本次结束仅发高层`bat stop`，最终READY/PWM0/RUN0且路径全关；不进入20 V。

### r30锁点修复：v48d Li-ion 22 V方向自主候选

v48d在下位机确定性调度器中增加待物理验证候选：Li-ion10S、CC、板端Vin≤22.0 V、工作输出39.0~42.5 V、Imax0.8~1.2 A时，从22 V/CV43 V已验证经验转移EPS0/44 ns、cap120 ns、PI2.0/0.2、slew0.05 A/s，运行时ID=`22040001`。该选择在`bat start`后由固件根据ADC和充电目标完成，不增加上位机底层指令，AI OFF仍完整。候选不预先进入生成的正式成功参数表，只有原22 V/CV40 V复测通过后才允许提升。

检查时发现r29单点PASS记录的`reuse_allowed=true`会让生成器误认为Li-ion整个功能已完成；已更正为false，数据和PASS结论保留，待Li-ion全部工况通过后再统一提升。参数生成器`--check`通过，正式表仍为charger/current/voltage=`29/13/2`，r30 FAIL未入表。

CPU0 ARM完整构建通过，text/data/bss=`178958/560/172405` bytes，ELF/SREC SHA256=`221940A2981C2363A9DA11E1F42D8FEA97AADBB4637F9CE14EFD551AB170FFA1`/`2F9AB2E4D7FB37208E2599BED0F7452879806BACE074364141D6C91A8E6169A7`。CPU1构建退出码0，产物不变，text/data/bss=`17190/132/9104` bytes，ELF/SREC SHA256=`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。上位机首次被Windows `py -3`误选Python3.9，因不支持`dataclass(slots=True)`在收集阶段失败，不是协议回归；改用项目要求的Python3.13后22/22 PASS。固化烧录脚本为`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v48d_liion_vin22_direction/flash_dualcore.jlink`。

升级前COM4只读确认旧v48c为Vin=21.35 V、READY/PWM0/RUN0/fault0、路径全关、ADC valid/invalid=`0x0000`；未访问电子负载。J-Link退出码0，CPU0 Bank0完成180224 bytes擦除、编程、校验，Bank1内容一致；CPU1内容一致。原始日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48d_liion_vin22_direction_dualcore_flash.log`。升级后板端回读版本`2026-08-20-dualcore-charge-vin22-direction-v48d`，Vin=21.35 V、READY/PWM0/RUN0/fault0、路径全关、ADC valid/invalid=`0x0000`，正式充电表仍29条。随后RK8510仅短连接只读确认mode=2、CV40 V、state=1，未写入或切换负载。升级和启动确认本身不记为功能PASS。

### r31 Li-ion 22 V/CV40 V v48d原点复测 PRETEST

人工工况保持22.0 V/5.0 A/ON、RK8510 CV40.0 V/LOAD ON，无需操作者再次换工况。测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48D_r31`；AI OFF、Li-ion10S、Vcv42.0 V、Imax/Ipre1.0 A、Iterm0.30 A与r30完全相同，仅固件变为v48d。必须由下位机自主选中`entry=22040001`和EPS0/44 ns、cap120 ns、PI2.0/0.2；主机底层命令计数必须为0。外部有效峰值≤1.30 A，末10电流0.9974~1.1024 A，CC、fault0、最终READY/PWM0/RUN0。未通过则继续锁定22 V/CV40 V，不进入20 V。

### r31 v48d 0/44 ns候选过流 FAIL，继续锁定22 V/CV40 V

v48d只接收高层充电目标，AI OFF，主机底层命令计数0；下位机按预期自主选中`entry=22040001`，全程EPS0/44 ns、cap120 ns、PI2.0/0.2、CC。但CV40工况下secondary inner=44 ns在raw=0时仍形成22 ns有效相移地板，外部电流无法再降低。

26个RK8510低频只读样本中21个为带载有效点；外部峰值2.003 A/80.083 W，末10为39.9820 V/2.0001 A/79.9676 W，范围1.997~2.003 A，明显高于1.1024 A上限和1.30 A启动峰值上限。结论`FAIL_LI_CC_CV40_VIN22_INNER_FLOOR_2P0001A_EXCEEDS_TARGET`。板端144个RUN记录末20点均值Vin=20.6545 V、Vout=39.3540 V、Iout=1.9775 A、Pin/Pout=83.850/77.835 W；最高温度29.3/28.6 ℃，fault非零0。结束高层停止后READY/PWM0/RUN0且路径全关。

下一轮不改变人工工况，将待验证secondary inner从44 ns缩小到20 ns，使最低有效相移从22 ns降到10 ns；该变更只减小开机功率，电流PI仍可通raw正向补足到1 A，不通过上位机调相移。新候选ID=`22040002`，继续不进入正式成功参数表，不进入20 V。

v48e参数生成器`--check`为charger/current/voltage=`29/13/2`且通过；CPU0 ARM完整构建通过，text/data/bss=`178998/560/172405` bytes，ELF/SREC SHA256=`BE50C023E8A635DF9A6A2952B6F2EF01D5652D2725742852D160F2D1EF8BDAA9`/`4CE7654BAE7F67F8EB06E1F775A39D6D46C4DE81073CE4EB3B91E79C36E9135C`。CPU1依赖构建退出码0，text/data/bss=`17190/132/9104` bytes，ELF/SREC SHA256仍为`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。Python3.13上位机协议/传输回归22/22 PASS。固化烧录脚本为`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v48e_liion_vin22_floor/flash_dualcore.jlink`。

升级前COM4只读确认v48d为Vin=21.35 V、READY/PWM0/RUN0/fault0、路径全关。J-Link退出码0，CPU0 Bank0完成180224 bytes擦除、编程和校验，Bank1匹配，CPU1匹配；原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48e_liion_vin22_floor_dualcore_flash.log`。升级后回读版本`2026-08-20-dualcore-charge-vin22-floor-v48e`，Vin=21.35 V、READY/PWM0/RUN0/fault0、路径全关、ADC valid/invalid=`0x0000`。升级本身不作为功能PASS。

### r32 Li-ion 22 V/CV40 V v48e 0/20 ns原点复测 PRETEST

人工工况仍为22.0 V/5.0 A/ON和RK8510 CV40.0 V/LOAD ON，测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48E_r32`。高层Li-ion目标与r30/r31不变，AI OFF；下位机必须自主选中`entry=22040002`、EPS0/20 ns、cap120 ns、PI2.0/0.2，主机底层命令计数0。外部有效峰值≤1.30 A、末10电流0.9974~1.1024 A、CC、fault0并最终READY/PWM0/RUN0；失败则继续锁定原点。

### r32 v48e 0/20 ns候选仍过流 FAIL，继续锁点

下位机按预期自主选中`entry=22040002`，AI OFF、主机底层命令计数0，EPS0/20 ns使有效相移地板降到10 ns。外部峰值1.750 A/69.970 W，末10为39.9843 V/1.7471 A/69.8562 W，范围1.745~1.750 A，仍明显高于1.1024 A上限和1.30 A峰值上限，结论`FAIL_LI_CC_CV40_VIN22_INNER_FLOOR_1P7471A_EXCEEDS_TARGET`。

板端143个RUN记录末20点均值Vin=20.6730 V、Vout=39.3510 V、Iout=1.7315 A、Pin/Pout=73.810/68.135 W；最高温度29.6/28.7 ℃，fault非零0，结束高层停止后READY/PWM0/RUN0且路径全关。数据说明只要secondary inner非零，当前CV40工况仍存在无法由单向raw抵消的过高功率地板。下一轮改为真正SPS、inner0/0、外相移从0开始，由下位机PI向上调到1 A；候选ID=`22040003`，继续不进入20 V。

v48f参数生成器`--check`为29/13/2且通过；CPU0 ARM构建text/data/bss=`178998/560/172405` bytes，ELF/SREC SHA256=`66D1BF984AA1C41943B89A00290FB81B283451A2D657DFD2700096ECF7F85E26`/`A803DE72D2D23F63C084BBAF66EF0CFBDC18BF789E5F394BE0FF971B23FA9549`。CPU1构建退出码0且哈希仍为`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。Python3.13上位机回归22/22 PASS。烧录脚本`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v48f_liion_vin22_sps/flash_dualcore.jlink`。

升级前COM4只读确认v48e为Vin=21.36 V、READY/PWM0/RUN0/fault0、路径全关。J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，Bank1和CPU1匹配；原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48f_liion_vin22_sps_dualcore_flash.log`。升级后版本`2026-08-20-dualcore-charge-vin22-sps-v48f`，Vin=21.34 V、READY/PWM0/RUN0/fault0、路径全关、ADC valid/invalid=`0x0000`。

### r33 Li-ion 22 V/CV40 V v48f SPS原点复测 PRETEST

人工工况与高层充电目标均不r30~r32相同，测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48F_r33`。下位机必须自主选中`entry=22040003`、SPS inner0/0、cap120 ns、PI2.0/0.2，AI OFF、主机底层命令0。为观察从零相移起步的电流PI收敛，采集40 s；外部峰值≤1.30 A、末10电流0.9974~1.1024 A、CC、fault0并最终READY/PWM0/RUN0。未通过则仍锁定22 V/CV40 V。

### r33 v48f零相移自然电流偏高且执行方向错误，FAIL并继续锁点

下位机按预期自主选中`entry=22040003`、SPS inner0/0、cap120 ns、PI2.0/0.2；AI OFF且主机底层命令计数0。CV40 V负载下零相移并不是零功率点，外部峰值1.445 A/57.776 W，末10点39.9835 V/1.4428 A/57.6874 W，范围1.441~1.445 A，超过24 V基准允许上限1.1024 A和启动峰值1.30 A，结论`FAIL_LI_CC_CV40_VIN22_SPS_ZERO_PHASE_NATURAL_1P4428A_WRONG_ACTUATOR_DIRECTION`。

板端183个RUN记录末20点均值Vin=20.6140 V、Vout=39.2650 V、Iout=1.4170 A、Pin/Pout=61.000/55.645 W；最高温度29.9/29.1 ℃，fault非零0。电流误差`1.0 A - 1.42 A`为负，但固件仍使用forward方向，raw被钳在0 ns，无法向能降低功率的正相移移动。r30已直接证明在同一22 V/CV40 V分区中，从EPS补偿点把raw增加20 ns会把电流降至0.0756 A，因此下一轮保留SPS0/0安全起点，只把该Li-ion中输入候选的板端执行方向改为reverse，使电流高于目标时固件自主增加raw并收敛至1 A。候选ID更新为`22040004`；当前人工工况不变，不进入20 V。

### r33锁点修复：v48g板端自主reverse执行方向

v48g只对`DAB_CHARGER_LIION_MID_ENTRY_ID=22040004`设置`current_control_direction=reverse`，并在初次高层调度和运行时自主重选两个入口同时应用；其他充电束仍为forward。SPS inner0/0、cap120 ns、PI2.0/0.2、slew0.05 A/s及上位机1.0 A目标均未改变，因而现场无需脚本提供相移或PI。参数生成器`--check`保持charger/current/voltage=`29/13/2`，失败点未提升。

CPU0 ARM构建通过，text/data/bss=`179038/560/172405` bytes，ELF/SREC SHA256=`4F946F2B40070F7A39961C19EB3AA65F158B3FE211BA8271BB48BF2D2D7C063B`/`FA0D9CD60E13876F77771659504F75B441D4C1ED6BA334F0AABFDA98744C388B`；CPU1构建通过且哈希仍为`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。Python3.13上位机协议/传输回归22/22 PASS。烧录前COM4确认旧v48f为READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，Bank1和CPU1匹配，日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48g_liion_vin22_reverse_dualcore_flash.log`。烧录后版本v48g，Vin=21.35 V、READY/PWM0/RUN0、路径全关、fault0、ADC invalid=`0x0000`。RK8510仅以1.2 s间隔短连接只读确认mode=2、CV40 V、state=1，未写入、未切换、未持续占用。

### r34 Li-ion 22 V/CV40 V v48g reverse原点复测 PRETEST

人工工况保持22.0 V/5.0 A/ON和RK8510 CV40.0 V/LOAD ON，无需操作者换工况。测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48G_r34`；AI OFF、高层Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3不变，主机仍禁止发送调制、inner、cap、PI、arch、sched、p、vinset和底层RUN命令。下位机须自主选中`entry=22040004`、SPS0/0、cap120、PI2/0.2和reverse方向。外部有效峰值≤1.30 A，末10电流0.9974~1.1024 A，CC、fault0并最终READY/PWM0/RUN0；失败继续锁点。

### r34 v48g reverse使SPS功率上升，提前终止并判FAIL

下位机自主选中`entry=22040004`、SPS0/0、cap120 ns和reverse方向，但该方向判断把r30的EPS600/0分区特性错误外推到了SPS分区。raw/effective phase升到120 ns后功率继续上升而不是下降；RK8510只读样本已到39.984 V/2.627 A/105.037 W且仍在上升，因此未等待完整40 s，立即中断两个采集进程并通过独立COM4短连接发送高层`bat stop`。15个负载样本中10个有效，截断窗口末10为39.9856 V/2.5117 A/100.4311 W、范围1.697~2.627 A；这不是稳态值，只记录失败上升过程。结论`FAIL_LI_CC_CV40_VIN22_REVERSE_SPS_RAISED_TO_2P627A_ABORTED`。

两份板端日志共81个RUN记录，停机前末20均值Vin=20.9620 V、Vout=39.8845 V、Iout=2.5990 A、Pin/Pout=111.560/103.630 W、effective phase=120 ns，最高运行温度29.0/27.6 ℃，fault非零0。强制高层停止后复查为Vin=21.36 V、READY/PWM0/RUN0、四路路径全关、fault0，停机后温度29.6/28.2 ℃。未向电子负载发送任何写命令。

修正结论：r30并未证明SPS正相移降功率；它证明的是EPS600/0 ns在22 V/CV40 V下从补偿点effective phase=0增加到20 ns，电流由近零只上升到约0.0756 A，即该分区是低功率正向执行器但20 ns上限不足。下一版恢复这一已有实测分区，保持forward方向，把phase cap从20 ns扩大到120 ns并沿用Li-ion原PI0.5/0.05；启动从预装补偿零有效相移开始，功率只随板端闭环目标向上增加。候选ID=`22040005`，继续锁定同一工况。

### r34锁点修复：v48h扩大已验证EPS低功率分区范围

v48h候选`22040005`使用EPS600/0 ns、forward、cap120 ns、PI0.5/0.05、slew0.05 A/s；初始束仍在PWM关闭时预装为补偿后的effective phase=0，主机只提供充电高层目标。参数生成器29/13/2通过且未提升失败点。CPU0构建text/data/bss=`178958/560/172405` bytes，ELF/SREC SHA256=`E496A61BED31572A4545F8A5048126666A9E3FB8E40246BAAAA54219BF339B61`/`BC640B71EFBFBB83B7F4F3F0F5971936D39337B351789B91BE2EDB3F7DABDE3D`；CPU1哈希不变，Python3.13上位机回归22/22 PASS。

烧录前旧v48g为READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，Bank1和CPU1匹配，日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48h_liion_vin22_epsrange_dualcore_flash.log`。烧录后v48h为Vin=21.35 V、READY/PWM0/RUN0、路径全关、fault0、ADC invalid=`0x0000`；RK8510仅短连接只读确认mode=2、CV40 V、state=1。

### r35 Li-ion 22 V/CV40 V v48h EPS范围原点复测 PRETEST

人工工况不变，测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48H_r35`。AI OFF且只发送Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3等高层命令；下位机必须自主选中`entry=22040005`、EPS600/0、forward、cap120、PI0.5/0.05。采集至少目标到达后10 s；外部峰值≤1.30 A、末10电流0.9974~1.1024 A、CC、fault0并最终READY/PWM0/RUN0。失败继续锁点。

### r35 v48h方向正确但EPS600 ns占用相移空间，低电流FAIL

下位机自主选中`entry=22040005`、EPS600/0 ns、forward、cap120 ns，AI OFF且主机底层命令0。外部28个样本中23个为CV40有效带载点；最大0.253 A/10.115 W，末10稳定为39.9833 V/0.2428 A/9.7076 W，范围0.242~0.243 A，结论`FAIL_LI_CC_CV40_VIN22_EPS600_CAP120_CURRENT_LOW_0P2428A`。板端158个RUN记录末20点均值Vin=21.1455 V、Vout=39.7300 V、Iout=0.2180 A、Pin/Pout=13.395/8.630 W；最高温度29.2/27.9 ℃，fault非零0，结束高层停止后READY/PWM0/RUN0且路径全关。

本轮证明forward执行方向正确：effective phase从20 ns扩到120 ns后外部电流由r30的0.0756 A升到0.2428 A。但primary inner=600 ns需要约300 ns原始相移作零点补偿，硬件原始相移绝对上限480 ns，最多只剩约180 ns有效相移；单纯继续增大软件cap不能获得所需范围。已测端点为inner0/SPS零相移1.4428 A和primary600/EPS补偿零点近0 A。下一轮在正常inner上限240 ns处建立中间分区，预期零点自然电流低于1.3 A且保留约360 ns原始相移余量，由forward PI在cap120 ns内向1 A收敛。候选ID=`22040006`，不改变人工工况。

### r35锁点修复：v48i EPS240中间分区

v48i把候选改为`entry=22040006`、EPS240/0 ns、forward、cap120 ns、PI0.5/0.05、slew0.05 A/s。CPU0构建text/data/bss=`179006/560/172405` bytes，ELF/SREC SHA256=`7A3CC92A0A14E33C4B8C113907DF365E183F936CC1449094E01C5FC3FE650009`/`CDD2B81443BCD384CF124BD9063717775879D43CB3BBF9EC1224CCDE88413521`；CPU1不变，生成器29/13/2和上位机22/22均通过。烧录前v48h为READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，CPU1匹配。烧录后v48i为Vin=21.35 V、READY/PWM0/RUN0、路径全关、fault0、ADC invalid=`0x0000`。电子负载工况未写入，沿用本轮开始前最近一次只读确认的CV40 V/LOAD ON。

### r36 Li-ion 22 V/CV40 V v48i EPS240原点复测 PRETEST

测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48I_r36`，人工工况和高层Li-ion目标不变，AI OFF。下位机须自主选中`entry=22040006`、EPS240/0、forward、cap120、PI0.5/0.05；主机底层命令0。外部峰值≤1.30 A、目标到达后至少10 s且末10电流0.9974~1.1024 A、CC、fault0、最终READY/PWM0/RUN0。失败继续锁点。

### r36 v48i进入正确平台但低一个量化步，FAIL并最小微调

下位机自主选中`entry=22040006`、EPS240/0、forward、cap120 ns，AI OFF且主机底层命令0。外部28个样本中23个为有效CV40带载点；峰值0.980 A/39.184 W。按统一末10规则为39.9840 V/0.9572 A/38.2723 W、范围0.820~0.980 A，其中包含到达平台前的爬升点；最后8个连续平台点为0.975~0.980 A、均值0.9771 A。两种统计均低于24 V比较下限0.9974 A，结论`FAIL_LI_CC_CV40_VIN22_EPS240_CAP120_PLATEAU_0P9771A_BELOW_BAND`。

板端158个RUN记录末20均值Vin=21.0620 V、Vout=39.7475 V、Iout=0.9485 A、Pin/Pout=42.595/37.695 W；最高温度29.2/28.3 ℃，fault非零0，结束高层停止后READY/PWM0/RUN0、路径全关。当前执行器方向、inner分区和闭环趋势均正确，且仅差约0.02 A；下一版只把cap从120 ns增加一个4 ns硬件量化步到124 ns，候选ID=`22040007`，其余参数保持不变。

### r36锁点修复：v48j增加一个4 ns cap量化步

v48j仅把EPS240/0候选cap从120 ns改为124 ns并更新ID为`22040007`，其余调制、PI、slew和高层目标不变。CPU0构建text/data/bss=`179006/560/172405` bytes，ELF/SREC SHA256=`5D30D7EA211A3F59333BF5E9DD1F18BC261E37D6F23054C281E80972ECED349F`/`871F7E4ADA0489F0BBA537C1A113CCEAE0F9F77BFA3EDDC82B9058184102BCB5`；CPU1不变，生成器29/13/2和上位机22/22通过。烧录前v48i为READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，CPU1匹配。烧录后v48j为Vin=21.35 V、READY/PWM0/RUN0、路径全关、fault0、ADC invalid=`0x0000`。

### r37 Li-ion 22 V/CV40 V v48j cap124原点复测 PRETEST

测试ID`CHG_VIN_03_LI_CC_CV40_V22_AUTONOMOUS_V48J_r37`。保持22 V/CV40 V，AI OFF，只发相同高层Li-ion目标；下位机须自主选中`entry=22040007`、EPS240/0、forward、cap124、PI0.5/0.05，主机底层命令0。采集38 s以取得目标到达后至少10 s平台；外部峰值≤1.30 A、末10电流0.9974~1.1024 A、CC、fault0并最终READY/PWM0/RUN0。失败继续锁点。

### r37 v48j 22 V/CV40 V自主CC PASS

v48j只接收AI OFF和Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3等高层命令，下位机自主选中`entry=22040007`、EPS240/0 ns、forward、cap124 ns、PI0.5/0.05；主机底层命令计数0。RK8510以1.2 s间隔短连接只读，共30个样本、26个有效CV40带载点；外部峰值1.026 A/41.023 W，末10为39.9830 V/1.0231 A/40.9060 W，范围1.021~1.026 A，落在24 V基准允许带0.9974~1.1024 A内。

板端173个RUN记录末段20点有效均值Vin=20.9484 V、Vout=39.6074 V、Iout=1.0021 A、Pin/Pout=44.742/39.700 W；最高温度29.5/28.3 ℃，fault非零0。结束只发送高层`bat stop`，最终READY/PWM0/RUN0且四路路径全关。结论`PASS_LI_CC_CV40_VIN22_EPS240_CAP124_AUTONOMOUS_V48J`，22 V锁点已修复。Li-ion功能尚需相同CV40 V的20 V输入点，因此本候选暂不提升到正式表，下一工况只把输入电源改为20.0 V，RK8510继续CV40 V/LOAD ON。

### r38 Li-ion 20 V/CV40 V输入不变性 PRETEST

操作者只把输入电源改为20.0 V/5.0 A/ON，RK8510保持CV40.0 V/LOAD ON。收到“完成”后COM4/COM24均不访问10 s；随后只读预检确认v48j、板端Vin=19.38 V、READY/PWM0/RUN0、路径全关、fault0、ADC invalid=`0x0000`，RK8510为mode=2、CV40 V、state=1，未写入或切换负载。

测试ID`CHG_VIN_04_LI_CC_CV40_V20_AUTONOMOUS_V48J_r38`。AI OFF并只发送与r37相同的Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3高层目标；下位机应自主选择当前中输入候选，主机不发送调制、inner、cap、PI、arch、sched、p、vinset或底层RUN命令。采集38 s，外部峰值≤1.30 A、末10电流0.9974~1.1024 A、CC、fault0并最终READY/PWM0/RUN0。失败锁定20 V/CV40 V原点修复，不完成Li-ion提升。

### r38 v48j瞬时输出窗口造成旧束自锁，FAIL

20 V输入下，下位机先按PRECHARGE/CC过程选择正式表旧束`881730593`及短暂候选`1041512235`，最终回到EPS600/0 ns、cap20 ns、PI0.5/0.05。该低功率束只能把输出维持在约36.6 V，而Li-ion中输入候选又要求当前测量Vout≥39 V，导致永远无法切换到已通过的EPS240/0、cap124分区。

RK8510共30个低频只读样本，最高电压38.225 V，最大电流/功率仅0.004 A/0.151 W；末10为36.5844 V/0.0030 A/0.109 W，电压范围36.583~36.587 V。板端174个SOFTSTART记录，末20有效均值Vin=19.1140 V、Vout=36.1995 V、Iout=-0.0153 A、Pin/Pout=3.353/-0.516 W；最高温度29.2/28.3 ℃，fault非零0，最终高层停止后READY/PWM0/RUN0、路径全关。结论`FAIL_LI_CC_CV40_VIN20_TRANSIENT_VOUT_WINDOW_STUCK_OLD_SEED`。

修复原则是用上位机已经给出的高层充电目标完成自主选束：Li-ion CC阶段、Vin≤22 V且配置Vcv在39.0~42.5 V时选择中输入候选，而不是要求瞬时Vout先达到目标附近。PRECHARGE仍可用正式表低功率启动；CPU1进入CC后便能脱离旧束。参数EPS240/0、forward、cap124、PI0.5/0.05保持不变，候选ID更新为`22040008`，继续原点复测。

### r38锁点修复：v48k按高层Vcv目标选择CC束

v48k将Li-ion中输入候选的输出窗口判断从瞬时`operating_voltage_v`改为上位机已配置的`battery_cv_v`，仍要求profile=Li-ion10S、stage=CC、Vin≤22 V、Imax0.8~1.2 A；因此不增加任何主机底层指令，也不会在PRECHARGE阶段强制高功率。候选参数仍为EPS240/0、forward、cap124、PI0.5/0.05，ID=`22040008`。

CPU0构建text/data/bss=`179022/560/172405` bytes，ELF/SREC SHA256=`FCB92346B1A20EDA5616F747EFFF6FBCB4C658EADAB63CB1959265C024C57464`/`4EA8853C8FE076CF866E6CD0EBF2531CA8C6D86B2331FA3D4C88518A34A0DF81`；CPU1不变，生成器29/13/2和上位机22/22通过。烧录前v48j为Vin=19.37 V、READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，CPU1匹配。烧录后v48k为Vin=19.38 V、READY/PWM0/RUN0、路径全关、fault0、ADC invalid=`0x0000`。

### r39 Li-ion 20 V/CV40 V v48k目标选束原点复测 PRETEST

测试ID`CHG_VIN_04_LI_CC_CV40_V20_AUTONOMOUS_V48K_r39`，人工工况和高层命令与r38完全相同。必须先低功率PRECHARGE，进入CC后由下位机自主应用`entry=22040008`、EPS240/0、forward、cap124、PI0.5/0.05；主机底层命令0。采集38 s，外部峰值≤1.30 A、末10电流0.9974~1.1024 A、fault0并最终READY/PWM0/RUN0。失败继续锁点。

### r39 v48k选束正确但SOFTSTART相移固定4 ns，FAIL

v48k只接收AI OFF和Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3等高层命令，下位机已自主选中`entry=22040008`、EPS240/0 ns、forward、cap124 ns、PI0.5/0.05；这证明按配置Vcv选束的r38修复有效。新暴露的阻塞点位于通用启动包络：`dab_startup_phase_limit_counts()`在SOFTSTART始终只返回1 count=4 ns，而状态晋级又要求Vout≥0.90×44=39.6 V。20 V输入下，4 ns不足以达到39.6 V，因而形成SOFTSTART等待RUN、RUN才允许相移爬升的循环锁死。

RK8510以至少1.2 s间隔短连接只读，共30个样本；最高36.927 V，最大电流/功率0.003 A/0.110 W，末10为36.9234 V/0.0030 A/0.1100 W，范围0.003~0.003 A。板端174个带电记录全部为SOFTSTART、RUN记录0；末20均值Vin=19.1335 V、Vout=36.5785 V、Iout=-0.0225 A、Pin/Pout=1.245/-0.800 W，实际`effective_phase=4 ns`且`startup_limit=4 ns`。最高温度28.9/28.5 ℃、fault非零0，结束仅发送高层`bat stop`，最终READY/PWM0/RUN0且四路路径全关。结论`FAIL_LI_CC_CV40_VIN20_SOFTSTART_PHASE_LIMIT_4NS_DEADLOCK`。

下一版只修启动包络，不改变已在22 V通过的候选参数：对已启用自动表、Energy Supervisor、电池启动有效、EPS/TPS可信束的智能充电路径，SOFTSTART先保持现有1 s/4 ns低功率窗口，随后沿用既有4 ns/100 ms斜率逐步放开到固件候选cap；其他手动、CV、CP、MPPT和非自动路径保持原逻辑。到达39.6 V后仍进入RUN并使用既有RUN包络，继续锁定20 V/CV40 V复测。

### r39锁点修复：v48l智能充电可信束软启动斜坡

v48l在`dab_startup_phase_limit_counts()`中加入严格限定的自动充电SOFTSTART分支：必须同时满足自动表已启用、表项有效、Energy Supervisor、电池start有效、inner由固件束管理且调制为EPS/TPS。满足时仍先保持1 s、4 ns低功率窗口，之后按原有4 ns/100 ms斜率放开到束内cap；其他路径仍在SOFTSTART保持4 ns。Li-ion中输入候选参数不变，仅为固件追踪把ID更新到`22040009`。

生成表检查29/13/2通过；CPU0构建text/data/bss=`179086/560/172405` bytes，ELF/SREC SHA256=`49E1DA79B6A5B4AECC1B146FD884926C7E1A973A35696375AB0AB4E1F3BCD5DD`/`DCBC087F032CD52DE7A104E38C0FE94F1F34D751BA82FAD0A6A17F0BDF767422`；CPU1仍为17190/132/9104 bytes，ELF/SREC SHA256=`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。Python3.13上位机串口/协议回归22/22 PASS。

烧录前v48k为Vin=19.38 V、READY/PWM0/RUN0、路径全关、fault0。J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，Bank1及CPU1内容匹配，日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48l_charge_softstart_ramp_dualcore_flash.log`。烧录后只读确认v48l、Vin=19.39 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效；未访问或写入电子负载。

### r40 Li-ion 20 V/CV40 V v48l软启动斜坡原点复测 PRETEST

测试ID`CHG_VIN_04_LI_CC_CV40_V20_AUTONOMOUS_V48L_r40`。保持20 V输入和RK8510 CV40 V/LOAD ON，只发送AI OFF、Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3及`bat start/stop`高层命令。下位机必须自主选择`entry=22040009`、EPS240/0、forward、cap124、PI0.5/0.05；预期SOFTSTART在1 s后按4 ns/100 ms逐步放开、进入RUN并稳定。采集38 s，外部峰值≤1.30 A、末10电流0.9974~1.1024 A、fault0，最终READY/PWM0/RUN0；失败继续锁点。

### r40 v48l软启动斜坡有效，但22 V的124 ns上限不足以覆盖20 V，FAIL

v48l只接收高层Li-ion目标并自主选中`entry=22040009`、EPS240/0、forward、cap124、PI0.5/0.05。SOFTSTART先保持4 ns，随后实际相移平滑爬升到124 ns，证明r39的启动包络死锁已经修复；但在20 V输入下达到cap后，外部输出仅39.27 V、板端约38.89 V，仍低于电子负载CV40导通点和板端RUN门限39.6 V，因而没有进入RUN，也无法建立1 A充电电流。

RK8510低频只读30点，最大39.270 V/0.004 A/0.157 W，末10为39.2694 V/0.0040 A/0.1570 W、范围0.004~0.004 A。板端174个带电记录全部SOFTSTART、RUN记录0；末20均值Vin=19.1260 V、Vout=38.8865 V、Iout=-0.0120 A、Pin/Pout=1.895/-0.515 W，最高温度28.6/28.3 ℃、fault非零0。结束仅高层停止，最终READY/PWM0/RUN0且路径全关。结论`FAIL_LI_CC_CV40_VIN20_EPS240_CAP124_BELOW_CV40_AND_RUN_THRESHOLD`。

按本轮实测斜率估算，effective phase 100~124 ns时板端Vout约从38.07升到38.89 V，补足到39.6 V约需22 ns；下一版为实测Vin≤20.5 V建立独立低输入分区，保留EPS240/0和PI0.5/0.05，只把cap增加到148 ns。22 V已通过的124 ns分区和参数保持不变；148 ns仍低于480 ns原始相移绝对上限且由相同软启动斜坡逐级到达。

### r40锁点修复：v48m增加Li-ion 20 V独立cap148分区

v48m按板端实测Vin确定Li-ion中/低输入分区：Vin≤20.5 V选择`entry=22040010`和cap148 ns，Vin>20.5 V继续使用22 V已通过的`entry=22040009`和cap124 ns。两者均为EPS240/0、forward、PI0.5/0.05、slew0.05 A/s，并使用v48l已验证的可信束软启动斜坡；上位机仍只给高层电池目标。

生成表29/13/2和上位机22/22回归通过。CPU0构建text/data/bss=`179174/560/172405` bytes，ELF/SREC SHA256=`B8CB6DC346FE602F5802CDBC3DA5D3F3778E717BD873470FB9E2518B74B6345A`/`EF49316D7776C1BEB65AC99ADC9847A322B23ED909DCBC0E048E7F5634A6B713`；CPU1哈希不变。烧录前v48l为Vin=19.38 V、READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 Bank0完成180224 bytes擦除/编程/校验，CPU1匹配。烧录后v48m为Vin=19.39 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效；未访问电子负载。

### r41 Li-ion 20 V/CV40 V v48m cap148原点复测 PRETEST

测试ID`CHG_VIN_04_LI_CC_CV40_V20_AUTONOMOUS_V48M_r41`。工况和高层命令与r40相同；固件须自主选择`entry=22040010`、EPS240/0、forward、cap148、PI0.5/0.05，并通过软启动斜坡进入RUN。采集42 s以保留进入RUN后二次包络和至少10 s稳定窗口；外部峰值≤1.30 A、末10电流0.9974~1.1024 A、fault0、最终READY/PWM0/RUN0。失败继续锁点。

### r41 v48m cap148仍差0.29 V，FAIL并追加16 ns实测裕量

固件自主选择`entry=22040010`并把effective phase平滑放开到148 ns，全程无冲击。RK8510低频只读33点，最大39.710 V/0.004 A/0.158 W；末10为39.7084 V/0.0040 A/0.1580 W。板端194个带电记录全部SOFTSTART、RUN记录0，末20均值Vin=19.1260 V、Vout=39.3235 V、Iout=-0.0110 A、Pin/Pout=2.065/-0.440 W；最高温度29.1/28.6 ℃、fault非零0，最终高层停止后READY/PWM0/RUN0、路径全关。结论`FAIL_LI_CC_CV40_VIN20_EPS240_CAP148_EXTERNAL_39P708_BELOW_CV40`。

与r40的124 ns/39.2694 V相比，增加24 ns使外部电压增加0.439 V；要从39.708 V越过CV40仍需约0.292 V，对应约16 ns。下一版只把20 V分区cap从148增加到164 ns，22 V的124 ns分区以及调制、inner、PI和软启动斜坡全部不变。

### r41锁点修复：v48n cap164

v48n仅把20 V独立分区cap从148 ns增加到164 ns并更新ID=`22040011`。生成表29/13/2、CPU0构建179182/560/172405 bytes、ELF/SREC SHA256=`C252C8A0EB04F463B3D6E6BBDD6D1D910320B45BC8F7CF9128ADC7D32C7CD4BB`/`038BD99BF5CFC55EF120CF0B51432DE132CC1E32EC0AD65F006A565F186894DF`，CPU1不变，上位机22/22通过。r41结束已确认v48m READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 180224 bytes擦除/编程/校验，CPU1匹配。烧录后v48n为Vin=19.37 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效。

### r42 Li-ion 20 V/CV40 V v48n cap164原点复测 PRETEST

测试ID`CHG_VIN_04_LI_CC_CV40_V20_AUTONOMOUS_V48N_r42`。工况和高层命令不变；固件须自主选择`entry=22040011`、EPS240/0、forward、cap164、PI0.5/0.05并进入RUN。采集46 s，要求外部峰值≤1.30 A、目标到达后末10电流0.9974~1.1024 A、fault0、最终READY/PWM0/RUN0；失败继续锁点。

### r42 v48n cap164外部39.852 V，FAIL

固件自主选择`entry=22040011`并平滑到164 ns。37个外部只读点中32个有效高压点，最大39.853 V/0.004 A/0.159 W，停机前末10有效点39.8516 V/0.0040 A/0.1590 W。板端214个带电记录全部SOFTSTART、RUN记录0；末20均值Vin=19.1350 V、Vout=39.4650 V、Iout=-0.0105 A、Pin/Pout=1.640/-0.360 W，最高温度29.5/29.0 ℃、fault非零0，最终READY/PWM0/RUN0且路径全关。结论`FAIL_LI_CC_CV40_VIN20_EPS240_CAP164_EXTERNAL_39P852_BELOW_CV40`。

相比148 ns，增加16 ns使外部增加约0.143 V；尚差约0.148 V，取20 ns硬件量化裕量，将20 V分区cap增加到184 ns。该步仍沿同一软启动斜坡，22 V cap124保持不变。

### r42锁点修复：v48o cap184

v48o仅把20 V分区cap从164增加到184 ns并更新ID=`22040012`。生成表29/13/2、CPU0构建179182/560/172405 bytes，ELF/SREC SHA256=`5376C014A7764CAF18A5F5AAD40BB81EDDCEF4D389334BE0BD7C77B6F8CD57A8`/`9B85B3A43306B3E54B26C39C58885D2F154AF26EE6D216AD1802EDAE7022461F`，CPU1不变，上位机22/22通过。r42结束READY/PWM0/RUN0、路径全关、fault0；J-Link退出码0，CPU0 180224 bytes擦写校验，CPU1匹配。烧录后v48o为Vin=19.37 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效。

### r43 Li-ion 20 V/CV40 V v48o cap184原点复测 PRETEST

测试ID`CHG_VIN_04_LI_CC_CV40_V20_AUTONOMOUS_V48O_r43`。工况和高层命令不变；固件须自主选择`entry=22040012`、EPS240/0、forward、cap184、PI0.5/0.05并进入RUN。采集50 s，外部峰值≤1.30 A、目标到达后至少10 s且末10电流0.9974~1.1024 A、fault0、最终READY/PWM0/RUN0。失败继续锁点。

### r43 v48o cap184进入收益饱和，FAIL并停止EPS cap扫描

v48o仅接收AI OFF、Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3和`bat start/stop`高层命令，下位机自主选择`entry=22040012`、EPS240/0、forward、cap184、PI0.5/0.05。相移沿可信束软启动斜坡到达184 ns，全程无冲击、fault=0；但仍未达到CV40负载导通点。

RK8510共40个低频短连接只读样本，其中36个高压点；最高39.858 V/0.004 A/0.159 W，停机前末10点均值39.8573 V/0.0040 A/0.1590 W。板端233个带电记录全部SOFTSTART、RUN=0，末20点均值Vin=19.127 V、Vout=39.439 V、Iout=-0.034 A、Pin/Pout=1.215/-1.335 W；最高温度29.8/29.2 ℃。结束仅发送高层`bat stop`，最终READY/PWM0/RUN0且路径全关。结论`FAIL_LI_CC_CV40_VIN20_EPS240_CAP184_EXTERNAL_39P858_BELOW_CV40`。

从164 ns到184 ns，外部稳态仅由39.8516 V升至39.8573 V，证明EPS240/0已经进入收益饱和，禁止继续盲目增加cap。改用同一块板、同一20 V输入已经通过的低输入参数束：TPS100/564 ns、forward、cap260 ns。该束在20 V/CV43/1 A自主测试中外部稳态1.0478 A、峰值1.062 A、fault0；本轮只把20 V Li-ion分区切到该实测束，22 V EPS240/0 cap124保持不变，并继续由固件自主软启动、闭环和停止。

### r43锁点修复：v48p切换同板20 V已通过TPS束

v48p将Li-ion低输入分区改为TPS100/564 ns、forward、cap260 ns、PI0.5/0.05、slew0.05 A/s，候选ID=`22040013`；20.5 V以上的22 V分区继续为EPS240/0 ns、cap124 ns、ID=`22040009`。TPS参数来自同板20 V/CV43/1 A自主PASS，不把24 V下已经拒绝的TPS点当作依据。主机仍只发送电池高层目标，AI OFF也不影响执行。

生成表一致性检查为29/13/2；Python3.13上位机串口/协议回归22/22 PASS。CPU0构建text/data/bss=`179206/560/172405` bytes，ELF/SREC SHA256=`56DFB8825D742184BBFAE54FDC0A121E46D2CB2B54F4538B522F72179A9CFB2C`/`01D8E2D32A4A2197CEEE90125B2AC3E144E55A20B4986B965734DB59E205F06E`；CPU1为`17190/132/9104` bytes，哈希仍为`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。Python3.9因不支持`dataclass(slots=True)`未进入测试，原错误保留，随后使用项目要求的3.13通过。

烧录前只读确认v48o为Vin19.37 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效。J-Link退出码0，CPU0 Bank0完成180224 bytes擦写校验，CPU1内容匹配；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48p_liion_vin20_tps_dualcore_flash.log`。烧录后只读确认v48p、Vin19.38 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效，未访问电子负载。

### r44 Li-ion 20 V/CV40 V v48p TPS100/564原点复测 PRETEST

测试ID`CHG_VIN_04_LI_CC_CV40_V20_AUTONOMOUS_V48P_r44`。物理工况继续锁定20 V输入、RK8510 CV40 V/LOAD ON；只发送AI OFF、Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3及`bat start/stop`。下位机须自主选择`entry=22040013`、TPS100/564 ns、forward、cap260、PI0.5/0.05。采集45 s，外部活动峰值≤1.30 A、末10点0.9974~1.1024 A、CC、fault0、最终READY/PWM0/RUN0；失败继续锁点。

### r44 v48p TPS100/564在CV40存在1.55 A自然地板，FAIL并停止

固件只接收高层目标并自主选中`entry=22040013`、TPS100/564 ns、forward、cap260 ns。该束很快进入RUN，但原始外相移已为0，内相移形成的232 ns有效相移自然地板仍产生过大电流。13个RK8510低频只读样本中9个活动点，电流1.522~1.559 A、均值1.5469 A，最大电流1.559 A，最大功率65.342 W；超过预定1.30 A启动上限后中断采集并另行发送高级`bat stop`，从未写入电子负载。

板端主日志与停止日志合并共有68个带电点（SOFTSTART 1、RUN 67），末20点均值Vin=19.059 V、Vout=39.8835 V、Iout=1.5355 A、Pin/Pout=66.715/61.255 W；最高温度29.0/28.4 ℃、fault非零0。停止后独立只读确认READY/PWM0/RUN0、路径全关、ADC有效、温度29.2/28.8 ℃。结论`FAIL_LI_CC_CV40_VIN20_TPS100_564_NATURAL_FLOOR_1P5469A_ABORTED`。

下一轮只把TPS secondary inner从564 ns减到536 ns，primary100 ns和cap260 ns不变，使零外相移的有效地板从232 ns降到218 ns。TPS100/536曾在更低板端Vin约17.06 V时安全运行且fault0，只因输出38.83 V不足启动；当前板端Vin约19.39 V具有更高电压裕量，因此将其作为相邻实测安全起点。若电流仍超带，继续只微调secondary inner，不回到已经饱和的EPS cap扫描。

### r44锁点修复：v48q TPS100/536构建、升级与r45 PRETEST

v48q仅把Li-ion 20 V分区的TPS secondary inner从564 ns减到536 ns并更新候选ID=`22040014`；primary100 ns、cap260 ns、forward、PI0.5/0.05、slew0.05 A/s以及22 V EPS240/0 cap124分区全部不变。生成表29/13/2、Python3.13上位机22/22通过。CPU0构建`179214/560/172405` bytes，ELF/SREC SHA256=`08A1DA656355E3324E53E38F8E3CE6754F1D7A7AA9B11B08883A7D96962AD554`/`861D4C455BE2ABD9236460C0FD3D9A23427973FC0C31DE747DE297AEBD519996`；CPU1哈希不变。

r44停止后已只读确认v48p处于READY/PWM0/RUN0、路径全关、fault0。v48q J-Link退出码0，CPU0 Bank0完成180224 bytes擦写校验，CPU1匹配；烧录后只读确认v48q、Vin19.38 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效。r45保持20 V/CV40 V原工况，只发高层Li-ion目标，采集45 s；要求`entry=22040014`、TPS100/536、活动峰值≤1.30 A、末10点0.9974~1.1024 A、fault0、最终READY。失败继续锁点。

### r45 v48q TPS100/536 20 V/CV40 V自主PASS

v48q仅接收AI OFF、Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3和`bat start/stop`高层命令，下位机自主选择`entry=22040014`、TPS100/536 ns、forward、cap260 ns、PI0.5/0.05。36个RK8510低频只读样本中31个活动点，活动峰值1.045 A/41.989 W；末10点均值39.9822 V/1.0423 A/41.6732 W，范围1.040~1.045 A，处于24 V基准1.0499 A对应的0.9974~1.1024 A允许带内。

板端209个带电记录为SOFTSTART 1、RUN 208，末20点均值Vin=19.1295 V、Vout=39.8685 V、Iout=1.0090 A、Pin/Pout=45.250/40.245 W；最高温度29.5/29.2 ℃、fault非零0。结束只发送高层`bat stop`，最终READY/PWM0/RUN0且路径全关。结论`PASS_LI_CC_CV40_VIN20_TPS100_536_AUTONOMOUS_V48Q`。

Li-ion固定CV40输入不变性三个主点现在全部通过：24 V=1.0499 A、22 V=1.0231 A、20 V=1.0423 A，最大差值0.0268 A，满足`max(0.05 A,5%)`判据。该组先保留为功能内PASS，待Li-ion负载CV轨迹和状态机验收完成后再把整套智能充电参数一次性正式提升并执行上位机联调。

### r46 22 V返回点预检无效：J-Link USB/COM4不在系统中

操作者完成20→22 V人工切换后，先按硬要求保持10 s无仪器访问。随后Windows串口仅有COM24/COM6/COM7，JLink CDC UART COM4和全部当前USB VID1366设备均不存在；板端只读脚本在打开串口前失败，因此未发送命令、未启动PWM。RK8510只执行一次低频测量查询并返回0 V，未写入或切换负载。无管理员权限的PnP重新扫描被Windows拒绝，重新枚举后COM4仍未恢复。本轮记为`INVALID_BOARD_COM4_NOT_PRESENT`，不是电气FAIL；r46必须在J-Link USB恢复后从预检重做。

J-Link USB重新插拔后COM4和USB VID1366复合设备恢复。只读复检确认v48q、板端Vin21.34 V、READY/PWM0/RUN0、路径全关、fault0、ADC有效；电子负载仅测量查询且板端关闭时为0 V。r46按加速要求采集20 s，仍只下发AI OFF和Li-ion高层目标，要求固件自主选择22 V候选`22040009`、EPS240/0、cap124，并用末10 s判定。

r46的20 s加速窗口中，固件正确自主选择`entry=22040009`、EPS240/0、cap124并保持RUN/fault0；外部最大1.022 A/40.869 W，但只有17.168 s和18.472 s两点进入目标带，总覆盖约2.6 s。原因是本地0.05 A/s参考斜坡本身接近20 s，属于采集设计不足，不是功能FAIL。该轮记`INVALID_CAPTURE_ONLY_2P6S_IN_TARGET_BAND`；同一工况立即补32 s r46 RETRY1，预留末10 s，不改参数或电子负载。

r46 RETRY1在最终v48q上完成20→22 V返回验证。固件自主选择`entry=22040009`、EPS240/0、cap124；26个负载低频只读样本中21个活动点，最大1.029 A/41.149 W，末10点连续11.744 s稳定为39.990 V/1.0266 A/41.053 W，范围1.023~1.029 A，满足24 V基准允许带。板端144个带电点中RUN 143，fault0，最高温度26.8/26.0 ℃，结束高层停止后READY/PWM0/RUN0、路径全关。J-Link重连后板端空载Iout约-0.32 A，故本轮板端末20活动值0.6885 A存在相同偏置并完整保留；外部RK8510只读电流作为本项验收主依据，负载从未写入。结论`PASS_LI_CC_CV40_V22_RETURN_AUTONOMOUS_V48Q`。

### r47 最终v48q Li-ion CV40 24 V返回点 PRETEST

操作者将输入22→24 V后保持RK8510 CV40 V/LOAD ON，主机按要求10 s不访问仪器。只读确认v48q、板端Vin23.32 V、READY/PWM0/RUN0、路径全关、fault0、控制遥测valid1。两次原始`adcdiag`分别为invalid0和0x0040，源码确认它是单个ADC结果寄存器硬件invalid位；相邻控制帧持续valid1、DMA运行且无fault，因此不按电气越界处理，运行日志继续逐帧观察。r47采集32 s，要求固件自主选择已验证24 V EPS600/0 cap20并执行prearm，外部峰值≤1.30 A、末10点0.9974~1.1024 A、fault0、最终READY。

### r47 最终v48q Li-ion CV40 24 V返回点 PASS

最终v48q仅接收AI OFF、Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3和`bat start/stop`高层命令，下位机自主选择`entry=769155114`、EPS600/0 ns、forward、cap20 ns并执行prearm。RK8510以至少1.2 s间隔短连接只读，26点中21点为活动点；活动峰值1.099 A/43.949 W，末10点连续11.750 s为39.9904 V/1.0922 A/43.6774 W，范围1.062~1.099 A，仍处于既定0.9974~1.1024 A带内。

板端145个带电点为SOFTSTART 1、RUN 144；末20点均值Vin=23.0685 V、Vout=39.8010 V、Iout=0.7520 A、Pin/Pout=30.875/29.925 W。J-Link重连后的约-0.32 A板端电流偏置仍然存在并如实保留，验收继续以RK8510只读电流为主；运行期间`valid=0`计数0、fault非零0，最高温度28.2/26.7 ℃。结束只发送高层`bat stop`，最终READY/PWM0/RUN0且四路路径全关，电子负载从未被写入或切换。结论`PASS_LI_CC_CV40_V24_RETURN_AUTONOMOUS_V48Q`。

Li-ion固定CV40输入回程20→22→24已全部在最终v48q通过；连同前向24→22→20数据，证明20~24 V输入变化时输出充电电流保持在既定允许带内。下一锁定点转入Li-ion高CV端状态链和充电电流随负载CV电压变化的友好趋势验证；在该点通过前不进入MPPT。

### Li-ion最终CV轨迹前板端零点复核

J-Link重连后r46/r47出现板端Iout相对RK8510固定偏低约0.32 A；该偏置不影响前两点的外部电流验收，却会把高CV端真实约0.36 A错误送入0.30 A终止判据。r47结束已为READY/PWM0/RUN0、四路路径全关，因此在不访问电子负载、不启动DAB的前提下执行一次板端`zero`诊断校准。v48q返回`[DAB][ZERO] PASS IIN=1.645V IOUT=1.643V IPRI=1.650V ISEC=1.652V`，随后Iout=-0.00 A、fault0、ADC invalid0、路径保持全关；原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48q_pre_li_cv42p2_zero_recal.log`。

后续最终轨迹已在主计划中提前锁定为CV40.0（已通过）→41.5→42.2→42.3 V。前两项每点至少22 s并取末10 s，42.3 V必须保留真实Iout≤0.30 A持续30 s后由下位机自主DONE/关断的完整证据；任一点失败立即锁定修复。下一测试ID为`LI_FINAL_01_LI_CV41P5_V24_AUTONOMOUS_V48Q_r48`，保持PSU24.0 V/5 A/ON、AI OFF和同一Li-ion高层目标，操作者只把RK8510改为CV41.5 V且保持LOAD ON；预计固件自主选择同板已验证`entry=478137810`、EPS600/0 ns、cap20 ns，电流应明显低于r47的1.0922 A而仍为正向，fault0并最终READY。

### r48 Li-ion最终轨迹 CV41.5 V PRETEST

操作者已把RK8510从CV40.0 V人工改为CV41.5 V，PSU保持24.0 V/5 A/ON、LOAD保持ON；随后完整保留10 s无仪器访问。低频只读预检确认RK8510 mode=2、CV=41.5、state=1；板端确认v48q、Vin=23.33 V、Iout=0.00 A、READY/PWM0/RUN0、四路路径全关、fault0、ADC valid且invalid0。r48只发送AI OFF及Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3和`bat start/stop`高层命令，采集22 s；固件应自主选择`entry=478137810`、EPS600/0 ns、cap20 ns。外部电流必须为正且明显低于r47末段1.0922 A，不得回升到CC上限，fault0并最终READY；失败锁定CV41.5 V修复。

### r48 Li-ion最终轨迹 CV41.5 V PASS

最终v48q只接收相同Li-ion高层目标并自主选中`entry=478137810`、EPS600/0 ns、cap20 ns；AI OFF且主机低层调制命令为0。RK8510低频短连接只读21点，其中16个有效带载CV点；最大0.779 A/32.706 W，末10点连续11.749 s为41.4982 V/0.7714 A/32.0111 W，范围0.770~0.774 A。相对r47 CV40末段1.0922 A下降0.3208 A，电流保持正向且无回升，符合电池电压升高时电流下降的友好趋势。

板端114个带电点为SOFTSTART 1、RUN 113；末20点均值Vin=23.1195 V、Vout=41.3545 V、Iout=0.7785 A、Pin/Pout=39.180/32.140 W。补偿电压尚未达到CV进入阈值，故合法保持CC，下一点再验证CV；fault非零0、`valid=0`计数0，最高温度28.2/27.1 ℃。结束只发送高层`bat stop`，最终READY/PWM0/RUN0且四路路径全关；负载从未被写入。结论`PASS_LI_FINAL_CV41P5_FRIENDLY_TREND_AUTONOMOUS_V48Q`。

### r49 Li-ion最终轨迹 CV42.2 V PRETEST

操作者已把RK8510从CV41.5 V人工改为CV42.2 V，PSU保持24.0 V/5 A/ON、LOAD保持ON，随后完整保留10 s无仪器访问。低频只读预检确认RK8510 mode=2、CV=42.2、state=1；板端确认v48q、Vin=23.33 V、Iout=0.00 A、READY/PWM0/RUN0、四路路径全关、fault0、ADC valid且invalid0。r49只发送AI OFF和同一Li-ion高层目标，采集26 s；固件应自主选择`entry=6157160`、EPS600/0 ns、cap20 ns并进入CV。外部电流必须保持正向且低于r48的0.7714 A，不得回升；若仍高于0.30 A则只判CV/taper，不强行判DONE。fault0并最终READY，失败锁定CV42.2 V修复。

### r49 Li-ion CV42.2 V进入CV但零inner产生高自然传输，FAIL并锁点

v48q只接收AI OFF及同一Li-ion高层目标，状态顺序为PRECHARGE→CC→CV；进入CV后调度器按实际工作电压选择相邻`entry=425116690`、EPS600/0 ns、cap20 ns。CV逻辑把primary inner从600 ns逐步降到0 ns，同时raw约从300 ns降到0 ns、遥测effective phase从约2 ns降到0 ns，但该EPS 0/0退化工作点并非低功率关断点，反而产生高自然传输。

RK8510低频短连接只读21点，其中17个有效带载CV点：电流先为0.575 A，随后依次升至0.929、1.310 A并在末段达到2.381 A/100.456 W；末10点连续11.762 s均值42.1892 V/1.7627 A/74.3668 W，范围1.110~2.381 A，既高于r48的0.7714 A又持续上升，明确违反友好降流趋势。板端114个带电点为SOFTSTART 1、RUN 113，其中CC帧9、CV帧100；末20点均值Vin=22.926 V、Vout=41.987 V、Iout=2.346 A、Pin/Pout=108.035/98.490 W。最高温度29.0/28.3 ℃，fault非零0、`valid=0`计数0。结束只发送高层`bat stop`，最终READY/PWM0/RUN0、四路路径全关；电子负载从未被写入。

结论`FAIL_LI_CV42P2_EPS_ZERO_INNER_HIGH_NATURAL_TRANSFER`。CV42.2 V原工况锁定，禁止进入42.3 V。修复方向不是继续减小inner，而是避开EPS 0/0退化点：复用本板历史CV42.2 V低电流实测束并让CV电流环在可控的非退化inner/outer组合上降流；必要时在目标电流接近0时由下位机自主执行无扰停机，而不是保持EPS 0/0带电。修复、构建、升级后必须在同一24 V/CV42.2 V条件复测。

### r49锁点修复：v48r Li-ion CV保留成形inner，LFP路径不变

r49逐帧证明电流从EPS600/0 ns、raw300 ns时约0.575 A开始，随primary inner 600→0 ns和raw 300→0 ns持续上升，最终到2.381 A；该板在M约0.91时的EPS0/0等价退化到已知高自然传输点。因此v48r只收窄`dab_validated_charger_zero_inner_for_taper()`：已通过的LFP profile在CV/DONE仍允许inner归零，Li-ion profile在CV/DONE保留固件自主选择的成形inner，继续由outer phase调节；DONE本地关闭PWM和四路路径逻辑不变。主机仍只给高层目标，AI OFF完整。

生成表一致性检查为charger/current/voltage=`29/13/2`；CPU0构建text/data/bss=`179166/560/172405` bytes，ELF/SREC SHA256=`AF0A87DA8A2C44A988FEBD54F8E77C4873E8A1A040671D7291CB1A1C49EF7364`/`E5F44E4453C675C625EBA280C5A5B0F67F56B9DD7730891DC071C17982B4C126`。CPU1为`17190/132/9104` bytes且ELF/SREC SHA256仍为`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`；Python3.13上位机协议/传输22/22 PASS。产物保存在`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v48r_liion_cv_shaped_inner/`。

烧录前只读确认旧v48q为Vin23.33 V、READY/PWM0/RUN0、路径全关、fault0、ADC valid/invalid0，未访问电子负载。J-Link退出码0，CPU0 Bank0完成180224 bytes擦写校验、Bank1匹配，CPU1匹配；原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48r_liion_cv_shaped_inner_dualcore_flash.log`。烧录后回读版本`2026-08-20-dualcore-liion-cv-shaped-inner-v48r`，Vin23.33 V、Iout0.01 A、READY/PWM0/RUN0、路径全关、fault0、ADC valid/invalid0。升级本身不作物理PASS。

### r50 v48r Li-ion CV42.2 V原工况复测 PRETEST

人工工况未改变：PSU24.0 V/5 A/ON，RK8510低频只读确认mode2、CV42.2 V、state1且LOAD ON。r50只发送AI OFF及Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3和`bat start/stop`高层命令，采集26 s；固件须在CV保持EPS600/0 ns成形束，外部电流保持正向、低于r48的0.7714 A且末段不得再上升，fault0并最终READY。失败继续锁定CV42.2 V，不进入42.3 V。

### r50 v48r Li-ion CV42.2 V原工况复测 PASS

v48r只接收AI OFF及同一Li-ion高层目标；固件自主选中`entry=425116690`并在CV全程保持经本板验证的EPS600/0 ns成形束，未再把inner降到退化的EPS0/0工作点。RK8510仍仅以至少1.2 s间隔短连接只读：21个采样中16个为有效带载点，峰值0.585 A/25.457 W；末10点连续11.750 s均为0.583 A，均值42.1894 V/0.5830 A/24.5957 W。相对r48 CV41.5 V的0.7714 A下降0.1884 A，相对CV40的1.0922 A下降0.5092 A，电流为正、稳定且无r49的后段上升，友好降流趋势成立。

板端113个带电RUN帧中CC 11帧、CV 98帧；`entry=425116690`出现95次、EPS inner=600 ns出现105次，CV末段raw约300 ns且effective phase为0，表明低电流来自非退化成形束而不是EPS0/0。末20点均值Vin=22.9985 V、Vout=41.8390 V、Iout=0.5595 A、Pin/Pout=30.505/23.415 W；fault非零0、`valid=0`计数0，最高温度28.5/27.4 ℃。结束只发送高层`bat stop`，最终READY/PWM0/RUN0且四路路径全关；电子负载从未被写入或切换。

结论`PASS_LI_FINAL_CV42P2_TAPER_SHAPED_INNER_AUTONOMOUS_V48R`，r49锁点关闭。下一锁定点为Li-ion CV42.3 V终止验证：必须以RK8510外部真值确认输出电流不高于0.30 A并连续保持30 s，随后由下位机自主进入DONE并关闭功率路径；若不满足则留在本点修复。

### r51 v48r Li-ion CV42.3 V自主DONE验证 PRETEST

操作者已把RK8510人工改为CV42.3 V并保持LOAD ON，PSU保持24.0 V/5 A/ON；随后完整保留10 s无仪器访问。低频、逐次开查询关串口的只读预检确认RK8510 mode2、CV42.3 V、state1；板端确认v48r、Vin23.32 V、Iout0.00 A、READY/PWM0/RUN0、四路路径全关、fault0、ADC valid。r51只发送AI OFF及Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3与`bat start`高层命令，采集50 s；必须由RK8510外部真值证明Iout不高于0.30 A连续30 s，且下位机自主进入DONE并关闭PWM/四路路径。结束阶段的`bat stop`只作未自主完成时的安全收尾，不得作为DONE通过证据；失败锁定42.3 V修复，不进入MPPT。

### r51 v48r Li-ion CV42.3 V电流地板高于Iterm，FAIL并锁点

v48r只接收AI OFF和同一Li-ion高层目标，下位机完成PRECHARGE→CC→CV并最终选择`entry=6157160`、EPS600/0 ns、raw300 ns、effective phase0 ns。RK8510以至少1.2 s间隔短连接只读40点，其中35点为有效带载点；活动峰值0.550 A/23.266 W，全部有效点均高于0.30 A，末10点连续11.762 s均值42.3001 V/0.5482 A/23.1884 W，范围0.548~0.549 A。电流稳定且无异常上升，但未达到终止阈值。

板端237个带电点为SOFTSTART 1、RUN 236，阶段帧PRECHARGE 3、CC 10、CV 221、DONE 0；末20点均值Vin=23.1380 V、Vout=42.1585 V、Iout=0.5545 A、Pin/Pout=30.760/23.375 W。CPU1电流命令已降为0 A，外环也处于effective phase0 ns，但EPS600/0 ns在当前新MOS和M约0.91下仍有约0.55 A自然传输地板，因此30 s终止计时根本未启动。fault非零0、`valid=0`计数0，最高温度29.9/28.3 ℃。

50 s窗口结束未出现`[BAT][AUTO][DONE]`，故结论为`FAIL_LI_CV42P3_EPS600_NATURAL_FLOOR_ABOVE_ITERM_NO_DONE`。结束阶段仅用预先声明的高层`bat stop`收尾；随后板端独立复核READY/PWM0/RUN0、四路路径全关，电子负载从未被写入或切换。CV42.3 V继续锁定，禁止进入MPPT。

r49从EPS600/0 ns向0/0 ns逐步减小primary inner的原始序列已经给出单调方向：上端600 ns约0.53~0.58 A，inner减小时电流持续增加。因此同一方向的低风险修复是仅在Li-ion进入CV/DONE后，由下位机把primary inner从已验证600 ns缓升到硬件允许范围内的680 ns，保持secondary0 ns、effective phase0和原表调度不变；680 ns低于已配置的720 ns绝对inner上限，且以现有0.04 ns/ms斜率无扰进入。该点必须构建、双核升级后在当前24 V/CV42.3 V原工况复测，外部电流≤0.30 A连续30 s并自主DONE才允许通过。

### r51锁点修复：v48s Li-ion CV专用680 ns成形束

v48s在正式固件内新增唯一受限的Li-ion终止成形规则：仅当profile=Li-ion10S、stage=CV或DONE、调制=EPS且表内primary inner不低于600 ns时，把运行目标设为680 ns；普通inner斜率0.04 ns/ms负责从600 ns平滑过渡。CC阶段、20/22 V低输入束、LFP零inner终止路径、表选择、outer current PI、DONE本地关断和AI OFF确定性路径均不变。680 ns对应raw零点补偿约340 ns，低于480 ns绝对raw上限；自身也低于720 ns绝对inner上限。

生成表一致性检查为charger/current/voltage=`29/13/2`，新增分支C编译检查PASS。CPU0构建text/data/bss=`179294/560/172405` bytes，ELF/SREC SHA256=`BD29E97ED71C1EB10B049125F44EF9308AD38348B028B2150FB03843E40DC25A`/`324B00A25D30CC61759AED350CE62494E45275ECBB77AC539510261DC0C92D18`；CPU1仍为`17190/132/9104` bytes，ELF/SREC SHA256=`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`。Windows默认`py -3`首次误选旧Python3.9并重现已知`dataclass(slots=True)`兼容错误；明确改用Python3.13后上位机协议/传输22/22 PASS。产物保存于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v48s_liion_cv_inner680/`。

烧录前沿用r51结束后的独立板端复核：READY/PWM0/RUN0、四路路径全关、fault0。J-Link退出码0，CPU0 Bank0完成180224 bytes擦写校验、Bank1匹配，CPU1匹配；日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48s_liion_cv_inner680_dualcore_flash.log`。烧录后读回版本`2026-08-20-dualcore-liion-cv-inner680-v48s`，Vin23.32 V、Iout0.00 A、READY/PWM0/RUN0、路径全关、fault0、ADC valid；RK8510低频只读仍为mode2、CV42.3 V、state1，未被写入。

### r52 v48s Li-ion CV42.3 V原锁点复测 PRETEST

人工工况未改变：PSU24.0 V/5 A/ON，RK8510 CV42.3 V/LOAD ON。r52仍只发送AI OFF和Li-ion高层目标及`bat start`；必须看到固件在CV自主把primary inner从600 ns缓升到680 ns，RK8510外部电流不高于0.30 A连续30 s，随后出现`[BAT][AUTO][DONE]`并由板端自行关闭PWM和四路路径。任何一项缺失都继续锁定42.3 V。

### r52 v48s Li-ion CV42.3 V方向正确但仍高于Iterm，FAIL

固件按设计在CV把primary inner从600 ns平滑扩到680 ns，raw补偿从300 ns变为340 ns、effective phase保持0 ns，无电流冲击。RK8510低频只读40点、35个有效带载点；启动峰值0.557 A/23.562 W，680 ns稳态末10点连续11.760 s为42.2990 V/0.3820 A/16.1579 W，范围固定0.382 A。相对r51的0.5482 A下降0.1662 A，证明修复方向正确，但仍高于0.30 A，不能启动终止计时。

板端236个带电点为SOFTSTART 1、RUN 235；阶段PRECHARGE 3、CC 9、CV 222、DONE 0，680 ns帧211个。末20点均值Vin=23.1680 V、Vout=42.2015 V、Iout=0.3570 A、Pin/Pout=19.760/15.010 W；最高温度29.6/28.3 ℃、fault非零0、`valid=0`计数0。50 s后未出现自主DONE，按计划高层`bat stop`收尾并回到READY/PWM0/RUN0、路径全关；负载从未被写入。结论`FAIL_LI_CV42P3_EPS680_CURRENT_0P382_ABOVE_ITERM_NO_DONE`，继续锁定42.3 V。

下一轮只把同一Li-ion CV专用成形目标从680 ns增至720 ns。720 ns是固件已有`DAB_INNER_SHIFT_MAX_COUNTS=180`乘4 ns的绝对边界，不突破硬件约束；对应零有效相移的raw补偿360 ns仍明显低于480 ns绝对raw上限。该变化只会沿已实测方向继续降低当前约16 W输出，不提高MOS功率应力，且继续使用0.04 ns/ms斜率。

### r52锁点修复：v48t Li-ion CV专用720 ns成形束

v48t只把v48s的Li-ion CV/DONE专用primary-inner目标从680 ns增至720 ns，仍由已有0.04 ns/ms斜率自主平滑过渡；720 ns等于固件既有绝对inner边界，零有效相移时raw补偿约360 ns，未超过480 ns绝对raw边界。CC、LFP、表选择、DONE本地关断以及AI OFF确定性路径均不变。

生成表一致性检查为charger/current/voltage=`29/13/2`；CPU0构建text/data/bss=`179286/560/172405` bytes，ELF/SREC SHA256=`48F886061C6F48B5455B22B5175C0E13236B5114B96E639BE9092FD0F9FEE225`/`ACD5FB195324D51E2512517302450444039F99980E48ADDF9BAE6B5893F2E00F`。CPU1仍为`17190/132/9104` bytes，ELF/SREC SHA256=`E842A156439491691AEBF6C60DE06AA80B1537219B99D04F9F50C961D33F176B`/`4BE91239212B1333372E7D6E180E5D1F1FA0AD67D3A3EA9A1C98FB33C1922799`；Python3.13上位机协议/传输22/22 PASS。产物保存于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v48t_liion_cv_inner720/`。

J-Link退出码0，CPU0 Bank0完成180224 bytes擦写校验，CPU1镜像匹配；原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48t_liion_cv_inner720_dualcore_flash.log`。刷写后读回版本`2026-08-20-dualcore-liion-cv-inner720-v48t`，Vin23.32 V、READY/PWM0/RUN0、路径全关、fault0。电子负载仅低频只读确认为mode2、CV42.3 V、state1，未发送任何设置或开关命令。

### r53 v48t Li-ion CV42.3 V原锁点复测 PRETEST

人工工况未改变：PSU24.0 V/5 A/ON，RK8510 CV42.3 V/LOAD ON。r53仍只给下位机AI OFF及Li-ion10S/Vcv42/Imax1/Ipre1/Iterm0.3与`bat start`高层目标，由固件自主选择表项、调制和720 ns终止成形束。必须以RK8510外部只读真值证明Iout不高于0.30 A连续30 s，随后出现下位机自主DONE且PWM和四路功率路径自行关闭。结束清理命令不能充当自主DONE证据；任一条件不满足都继续锁定42.3 V并修复，不进入MPPT。

### r53 v48t Li-ion CV42.3 V外部Iterm驻留与自主DONE PASS

v48t只接收AI OFF与同一Li-ion高层目标，下位机自主完成PRECHARGE→CC→CV，在CV把primary inner平滑扩至720 ns，raw补偿360 ns、effective phase0 ns。RK8510仍以至少1.2 s间隔逐次开查询关串口只读：40个样本中26个为带载点，启动段峰值0.554 A/23.540 W；末10个带载点连续11.763 s全部为0.256 A，均值42.3015 V/0.2560 A/10.8290 W。

外部第一个确认Iout不高于0.30 A的样本位于8018 ms；之后所有带载只读点均不高于0.30 A，38089 ms首次确认输出已由下位机关闭，外部证据跨度30.071 s。板端在主机结束阶段命令之前已输出唯一`[BAT][AUTO][DONE]`，并明确记录`PWM=0 RUN=0 four path MOS=OFF`；因此DONE来自下位机驻留计时和本地关断，不是末尾`bat stop`清理命令。

板端177个带电帧为SOFTSTART 1、RUN 176，阶段帧PRECHARGE 3、CC 10、CV 163、DONE 1；720 ns帧147个，主要表项`425116690`出现159次。带电末20点均值Vin=23.0340 V、Vout=41.8910 V、Iout=0.2210 A、Pin/Pout=14.310/9.235 W；fault非零0、`valid=0`计数0，最高温度29.9/28.9 ℃。最终独立状态为READY/PWM0/RUN0、四路路径全关。结论`PASS_LI_CV42P3_EXTERNAL_ITERM_30S_AUTONOMOUS_DONE_V48T`，Li-ion CV42.3锁点关闭；电子负载全程未写入或切换。

至此智能充电的两种电池实物工况、20~24 V范围、友好电流趋势、AI OFF、本地参数束选择和自主DONE均完成。v48t正确参数已经构建、哈希、双核升级并在实物代表点生效；进入MPPT前只剩智能充电串口上位机闭环门禁，必须由上位机仅下发高层profile/目标/start/stop并核对实时状态与保存数据。

### 智能充电联调前上位机按四阶段计划收敛

操作者要求先修改上位机界面，删除测试计划不存在的功能。v0.3-plan-scope界面现只暴露智能充电、光伏MPPT、CP能源路由、精准电流四个主功能，通信入口固定为COM4/115200/8N1；电池列表只保留已实测Li-ion10S与LFP12S。独立稳压、铅酸/自定义、CAN、Ethernet、离线模拟、MPPT-充电协同、在线自动整定、工程解锁、原始命令和演示预设均从界面删除。

计划内功能保留并按模式动态显示：智能充电仅显示CV/Imax与两种profile；MPPT显示fixed/P&O/INC、功率/Pmax及计划内调制测试策略；CP显示42 V名义目标、功率、Pmax、SPS/EPS/TPS/AUTO与主动阻尼；精准电流显示Iref、Pmax和本地调制策略。界面固定显示AI OFF，并明确状态机、参数束、调制过渡和闭环均由下位机本地执行。“高级算法”改名为“本地控制状态”，只保留计划内VAD宽输入、MSW自动过渡和CPD稳定控制观察卡；JSONL记录与CSV导出保留。

串口高层映射同步补齐：所有功能配置首条为`ai off`；MPPT固定20.0~23.5 V、step0.20 V、interval250 ms并下发Pmax；精准电流AUTO路径下发`arch current`、Iref、`vinadapt on`和`sched auto`，不发送inner/cap/PI；智能充电仍不发送mod/arch/sched或底层参数。自测27/27 PASS，全部Python文件py_compile PASS；初次隐藏窗口检查因检查脚本在应用已关闭后重复destroy而退出1，修正检查脚本后退出0，并确认四模式可见字段分别为charge=voltage/current、MPPT=power/power_limit、CP=voltage/power/power_limit、current=current/power_limit。

关键文件SHA256：`app.py`=`B56EE6BB949A154C8853F1BFDDDFE3BA9949B831D1434C9C05A67D8029071015`，`ui.py`=`637DD132C88C31BC04D341059CE488EB09C5A8FFD38B1B64DCCC04A332D36DC7`，`protocol.py`=`52CF75C62DB72E9EB03901AE80C71A342442DC9D269F8442FE699FFA63FE6AC6`，`README.md`=`9BAF15A36F5F35AEFC00DC9B38C68300C05F8EBC26EBAE9AC34BC7D4F397191C`。下一步在板端READY状态用该上位机真实串口链路执行智能充电配置读取、应用、高层start/自主DONE、记录与stop闭环；不访问电子负载设置。

### v48t智能充电正式参数整合复核与操作者上位机交接

操作者要求智能充电完成后更新固件并把正确参数整合进去。源代码与正式参数表复核确认v48t已经包含Li-ion10S的20 V TPS100/536 cap260 entry22040014、22 V EPS240/0 cap124 entry22040009、24 V EPS600/0 cap20，以及CV/DONE专用720 ns成形；LFP12S的20/22/24 V CC与CV44/DONE记录均在29条正式charger表中。新增`artifacts/2026-08-20_v48t_liion_cv_inner720/FINAL_SMART_CHARGE_PROMOTION.md`列出测试点、参数束、构建和哈希。

再次运行参数生成器`--check`得到charger/current/voltage=`29/13/2` PASS；CPU0完整增量构建text/data/bss=`179286/560/172405`，CPU1=`17190/132/9104`。双核ELF/SREC哈希逐字等于先前已经烧录且完成r53实物PASS的v48t产物，因此没有重复烧写相同镜像。板端最终只读回读版本仍为`2026-08-20-dualcore-liion-cv-inner720-v48t`，紧凑遥测state=READY、PWM=0、fault=0；COM4已关闭释放，原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v48t_final_smartcharge_promotion_postcheck.log`。

上位机又增加电池profile联动：选择Li-ion自动填42.0 V/1.0 A，选择LFP自动填43.8 V/1.0 A。界面自测27/27、battery preset隐藏窗口检查均PASS；最终`ui.py` SHA256=`C84E0B8818F3D64A96FD760DD2FA1AF67DF04379D520C89264082A73385D17B4`，`protocol.py`仍为`52CF75C62DB72E9EB03901AE80C71A342442DC9D269F8442FE699FFA63FE6AC6`。按操作者要求不再代操作GUI，由操作者直接通过四功能现场上位机完成智能充电串口门禁。

### 四功能上位机左侧说明块移除

按操作者截图要求，已将左侧的AI OFF说明、COM4现场链路说明、设备版本/能力说明整块从界面移除，同时删除相关的连接、复位和身份更新控件引用。固定COM4/115200通信、AI OFF命令和设备身份解析逻辑保留，仅不再额外占用界面空间。

`python app.py --self-test` 27/27 PASS，`py_compile` PASS；隐藏Tk窗口遍历108个文本控件，确认不再包含`AI：OFF`、`现场链路：`、`设备：等待`或`能力：文本命令`。更新后`ui.py` SHA256=`4302F39186ED51D02E95A8077EDFEE57562E1AEC95898AE242AEF85189C9CB86`。本次未访问板卡或电子负载。

### 智能充电界面取消手动充电电压/电流

操作者指出智能充电不应让上位机任意填写目标电压和电流。现在智能充电模式的全部目标输入块均不显示，界面只保留已实测的10S Li-ion或12S LFP电池类型选择。启动确认窗口也不再显示可人工设置的充电V/I/W，只说明下位机已验证参数束。

串口命令规划增加二次锁定：Li-ion10固定42.0 V/1.0 A，LFP12固定43.8 V/1.0 A，它们与已完成实测的参数一致。即使代码内部构造47.0 V/9.0 A的智能充电配置，最终UART仍只会产生选定profile的已验证数值；不支持未实测电池类型。电子负载CV只用于模拟当前电池端电压，充电电流仍由下位机状态机根据端电压、电池类型和参数束自主决定。

回归结果：自测28/28 PASS，`py_compile` PASS，隐藏Tk界面检查证明智能充电时可见目标字段为空、只显示battery选择器，且任意V/I无法到达UART。`ui.py` SHA256=`1FF824570CE0888CA4FB094F07A934CDBA13F72DC8541FB5939A7E5BC06A63D3`，`protocol.py`=`D09ADB1B4D0B4021AF52F8CC6553B3F4F032D61084EE01B6C285A64C22A88AE3`，`README.md`=`CF7426F4564C53E00189B2109C6F9F114B1CE27A03A34025EAA63807B2FB6287`。本次未访问板卡或电子负载。

### 智能充电现状保留决定

操作者决定智能充电功能暂按当前状态保留，不再继续扩展或修复。本决定不改写已有证据边界：当前上位机仍在两个电池profile上锁定已实测的1 A低功率参数束；Li-ion Vin24 V/CV29 V已观察到约84 W，且历史上该直接启动点已被标记为`REJECTED_WORKPOINT`；Li-ion 24 V的已提升记录主要为EPS，因此上位机有效调制显示持续EPS。

当前功能只作为已完成实测区间的低功率演示实现，不声称CV29预充已通过，不声称已按真实电池容量/BMS完成充电电流定型，也不声称已在同一输入工况下完成SPS/EPS/TPS的效率和温升综合排序。本次只记录范围决定，未修改代码/固件，未访问板卡、串口或电子负载。

### r54 LFP12 CV30预充异常重新打开智能充电锁点

操作者在四功能上位机联调中报告：LFP12、电子负载CV30 V时输出电流约5 A。该点低于LFP12的预充阈值32.4 V，按算法应为PRECHARGE并请求Ipre=1 A，因此5 A明确记为`FAIL_LFP_PRECHARGE_CV30_HIGH_CURRENT_REPORTED`，撤销“智能充电全部完成”的范围结论；按既定硬约束，在同一CV30工况修复并复测通过前不得进入MPPT。

上位机断开COM4后，板端纯只读快照确认DAB已处于READY/PWM0/RUN0、fault0，Vin约21.45 V、Vout约0.10 V；初始主桥温度约56 ℃并在关断后下降。随后仅在DAB关闭状态下重新下发AI OFF、LFP12、Vcv43.8 V、Imax/Ipre=1/1 A、Iterm0.3 A并独立`bat show`验证：profile=LIFEPO4_12S_43P8V、stage=OFF、start=0、Imax=1.00 A、Ipre=1.00 A、Vpre=32.40 V、Vcv=43.80 V，CPU1配置队列dropped=0。说明异常不是目标被设成5 A，而是低输出电压下的执行器/参数束问题。

源码复核确认当前v48t把OFF、IDLE、PRECHARGE统一归一为CC选束，且29条正式charger表只有LFP CC/CV高压点，没有PRECHARGE参数。初始Vout低于5 V时调度器还会用Vcv43.8 V代替实际端电压，因此启动会选择43 V附近的CC束；历史同类CV29直接启动已出现EPS 3.621 A/105.009 W与TPS 3.867 A/112.131 W，均为REJECTED_WORKPOINT。为避免重复已知高电流，不原样复现5 A；修复从v48t已验证的EPS高primary-inner降低自然传输地板方向开始，新增LFP PRECHARGE专用低功率束，再在锁定的CV30/LOAD ON工况逐步实测。

### r54 LFP12 CV30预充修复 PRETEST

操作者已将RK8510人工设为CV30.0 V并保持LOAD ON，输入电源保持现状；从操作者“完成”起保留至少10 s无仪器访问。测试只允许主机发送LFP高层参数与`bat start/stop`，下位机必须自主选择PRECHARGE束。首个修复种子使用EPS、primary/secondary inner=720/0 ns、phase cap20 ns、Kp/Ki=2.0/0.2、slew0.05 A/s，来自同板v48t在高inner方向连续降低功率的实测经验；若输出不足，只沿降低primary inner的方向小步调节。不得向RK8510写入或切换，板端ADC高频记录，RK8510仅在COM24恢复时以不高于1 Hz短连接只读。

### r54 v48u LFP CV30首轮复测 FAIL，锁定原点

v48u把Li-ion与LFP的OFF/IDLE/PRECHARGE优先映射到独立低功率预充束。参数表29/13/2检查和ARM C编译检查通过；CPU0构建text/data/bss=`179430/560/172405`，ELF/SREC SHA256=`6F35D5DEF9C7506D3676B7C5BFAD024DD984C5D5BF32F1D95BBD04B5AF359D61`/`C6EDEF4495048111885FC835DDB2B6C9820BC2DACB3C99E1F79180B19DD3D5F1`，CPU1仍为`17190/132/9104`。刷写前READY/PWM0/RUN0、温度约34.2/30.5 ℃；J-Link退出码0，CPU0 180224 bytes擦写校验、CPU1内容匹配。刷写后版本`2026-08-20-dualcore-dual-precharge720-v48u`，继续保持READY/PWM0/RUN0、四路路径全关、fault0。

人工工况保持输入约22 V、RK8510 CV30.0 V/LOAD ON，主机只发送AI OFF、LFP12、Vcv43.8 V、Imax/Ipre1.0/1.0 A、Iterm0.3 A及高层`bat start/stop`。启动时下位机正确预装`entry=30022001`、EPS720/0 ns、cap20 ns且PWM关闭时effective phase0；但输出接通瞬间板端Vout短暂读到约40.5 V，使状态机进入CC。随后CV负载把Vout钳回约30 V，状态机没有CC→PRECHARGE回退，运行调度器在1 s确认后错误应用正式CC表项`416259073`、EPS0/44 ns、cap120 ns。板端Iout由约1.94 A持续升至4.13 A，Pout最高约124.2 W；12 s预定短窗口到点后立即发送高层`bat stop`，确认回到READY。最高温度约34.0 ℃、fault0。结论`FAIL_LFP_CV30_STARTUP_SPIKE_LATCHED_CC_4P13A`，原点继续锁定，不进入后续工况。

v48v修复范围只增加确定性状态机回退：Li-ion与LFP处于CC时，若Vout连续100 ms低于各自Vpre减0.20 V，则自主回到PRECHARGE并重新取得预充束；短去抖拒绝单个ADC尖峰，0.20 V回差避免边界抖动。Lead-acid路径不变。主机测试新增LFP 40.5→30.0 V和Li-ion 40.0→29.0 V两条启动尖峰回退用例；版本更新为`2026-08-20-dualcore-dual-precharge-recover-v48v`，仍在相同CV30工况复测。

### r55 v48w LFP CV30动态状态与TPS720/180短测 FAIL，继续锁点

v48w把两种锂电profile的启动统一置为PRECHARGE，并增加PRECHARGE→CC 500 ms去抖、CC/CV→PRECHARGE 100 ms低压回退以及CV→CC 500 ms回退；状态由下位机根据最新Vout持续判断，不再由启动瞬间一次锁死。预充执行束改为本地变比规则在M约0.70处给出的TPS720/180 ns、cap20 ns、zero-effective-phase。生成表检查为29/13/2，ARM目标C编译检查通过；CPU0 text/data/bss=`179382/560/172405`，ELF/SREC SHA256=`2470FB4B30FA502C5AEFC0673DA49CC0C6BC8B28755C2568C0BF8BD0D7748CAE`/`04BB2A35ACA7C13D73B3E8C9BA4C1582A1374CDD30A394A99C38A18E91CDFB6F`。CPU1沿用v48v动态状态镜像，ELF/SREC SHA256=`9C86D837BD1DB6B615526E6B6E94D0A4E727FEF4511174E82DA1F2CAEAE72A05`/`3A2DD710C1B925C245F9F80B62DB8ECBF1A69FF30EAAEFFB3E5B3BA04249B10A`。J-Link退出码0，刷写后回读版本正确、READY/PWM0/RUN0、fault0。

同一人工工况保持输入板端约21.4 V、RK8510 CV30.0 V/LOAD ON；未写入或切换电子负载。主机只发送AI OFF、LFP profile、Imax/Ipre1 A、Iterm0.3 A和高层start/stop。启动尖峰Vout约40.7 V后回落30 V，正式CC候选虽被观察到但没有`applied`，固件持续保留entry30022001 PRECHARGE束；说明动态状态锁点修复按预期生效。TPS720/180把自然电流从v48u约1.90 A降至1.65~1.67 A，Pout约49.3~49.8 W，最高温度30.6 ℃、fault0。仍高于1 A目标，结论`FAIL_LFP_CV30_PRECHARGE_TPS720_180_FLOOR_1P67A`，不进入下一工况；下一步只沿已实测降流方向把secondary inner增至360 ns，保持cap20 ns和短窗口。

### r56 v48x LFP CV30 TPS720/360方向复核 FAIL

v48x只把PRECHARGE secondary inner由180 ns改为360 ns，primary720 ns、cap20 ns及动态状态逻辑不变。板端在同一约21.4 V输入和人工CV30工况明确回报`stage=PRECHARGE`、entry30022001、TPS720/360 ns、effective phase0；高压CC候选未应用。实测Iout为1.82~1.85 A、Pout约54.4~55.4 W，最高温度30.7 ℃、fault0，说明180→360 ns越过低点并使电流回升。结论`FAIL_LFP_CV30_PRECHARGE_TPS720_360_1P83A`，不再沿该方向增加secondary inner。

当前物理最低点仍是TPS720/180 ns约1.66 A。根因收敛为外电流PI把有效相移限制在非负区间，测量电流高于1 A时只能饱和在0 ns，无法抵消零有效相移处的自然传输。下一版回到TPS720/180，并只对两个PRECHARGE entry增加-8 ns（两个PWM计数）的本地有效相移偏置，使PI可在约-8~+12 ns内自主调节；原始相移仍远低于480 ns绝对边界。

### r57/r58 v48y有符号有效相移探针未改善，锁点继续

v48y回到TPS720/180并在两个PRECHARGE entry增加-8 ns本地有效相移起点。自主短测中板端明确回报PRECHARGE和effective=-8 ns，Iout约1.69~1.72 A、Pout约50.3~51.3 W、最高温度30.1 ℃、fault0，较0 ns没有改善。随后在同一固件上只做标定用途的底层`ph 16`探针；该命令清除正式schedule，因此不能作为功能PASS，只用于判断局部斜率。完全到位后raw约288 ns、TPS720/180等效约+16~18 ns，Iout仍约1.64~1.65 A。结论：约-8~+16 ns外相移区间几乎不能把预充降到1 A，不在此方向继续。

历史r51~r53已证明在EPS成形区把primary inner从600→680→720 ns可连续降低自然传输。720 ns普通上限来自同时预留480 ns绝对外相移的通用最坏组合；当前PRECHARGE cap仅20 ns，按实际四路边沿分别核算，primary760 ns时最晚边沿仍低于周期末约504 ns，且另有配置的400 ns死区。下一版增加“仅PRECHARGE表项可用”的primary专用上限832 ns，并在PWM驱动层按实际`max(primary, raw+secondary)`再次验证边沿；首点只使用760 ns，不跨步到上限。

### r59 v48z LFP CV30 TPS760/180继续降流，未到1 A

v48z只把PRECHARGE primary inner从720 ns增至760 ns，secondary180 ns、effective phase0、cap20 ns、PI和动态状态逻辑均不变。PWM驱动新增实际边沿校验，760 ns时primary最晚边沿距周期末约504 ns，配置的400 ns死区保持不变。板端在同一约21.4 V/CV30工况明确回报stage=PRECHARGE、entry30022001、TPS760/180；正式CC候选未应用。Iout由r55约1.65 A降至1.46~1.51 A，Pout约43.7~45.1 W，最高温度30.0 ℃、fault0。方向有效但仍高于1 A，结论`FAIL_LFP_CV30_PRECHARGE_TPS760_180_1P47A`；下一点只把primary增至800 ns。

### r60 v49a LFP CV30 TPS800/180约1.31 A，继续最后边界点

v49a只把PRECHARGE primary inner从760 ns增至800 ns。板端在同一工况持续回报PRECHARGE、entry30022001、TPS800/180、effective phase0；正式CC候选仍未应用。稳态Iout约1.31 A，启动带载首点1.38 A，Pout约38.9~41.1 W，最高温度30.3 ℃、fault0。相较760 ns继续稳定降流，已接近不要求高精度的1 A预充趋势；为不留下明显安全降流裕量，下一点使用预先静态核算并由驱动动态复核的最后专用边界832 ns，其他变量不变。

### r61/r62 v49b LFP CV30动态预充板端PASS

v49b只把PRECHARGE primary inner从800 ns增至已静态和运行时双重边沿校验的832 ns，secondary180 ns、effective phase0、cap20 ns、PI与状态机不变。r61短测Iout由启动带载首点1.26 A收敛至1.17~1.20 A；r62追加12 s确认的最后8个带载板端样本平均Vin=21.035 V、Vout=29.77125 V、Iout=1.1775 A、Pin=42.5125 W、Pout=35.0375 W，Iout范围1.17~1.19 A，最高温度30.8 ℃、fault0。全程stage=PRECHARGE，启动Vout约40 V瞬态后高压CC候选没有应用，证明状态持续依据最新端电压判断。结束后独立回读版本v49b、READY/PWM0/RUN0、四路路径全关、fault0。

结论`PASS_BOARD_ADC_LFP_CV30_DYNAMIC_PRECHARGE_1P18A_V49B`：相对原报告约5 A已降到接近1 A的电池友好预充范围，满足智能充电“不要求高精度但规律合适”的验收边界。RK8510从未被写入或切换；由于COM24未出现，1.1775 A为板端ADC证据而非电子负载外部读回，外部显示值需操作者现场观察确认。v49b源代码、构建哈希、双核镜像和原始日志已实时保存。下一锁点为Li-ion10S同类低压预充，需操作者把电子负载手动设为CV29.0 V并保持LOAD ON后再测。

### r63 v49b Li-ion CV29启动瞬态触发CC绕行，FAIL并继续锁点

操作者把RK8510手动设为CV29.0 V并保持LOAD ON后，主机完整留出10 s无仪器访问窗口；随后仅通过COM4发送AI OFF、Li-ion10S、Imax/Ipre 1.0/1.0 A、Iterm 0.3 A与高层`bat start/stop`，没有访问、写入或占用电子负载。静止检查为v49b、Vin约21.42 V、READY/PWM0/RUN0、四路功率路径关闭、fault0。

启动时下位机先正确选择`entry=29022001`、PRECHARGE、TPS832/180 ns、cap20 ns，但板端Vout连接瞬态升至约40.24 V并持续满足现有500 ms门槛，使状态短暂进入CC；运行调度器随后应用`entry=22040009`、EPS240/0 ns、cap124 ns。Vout回落约28.9 V后状态机能依据最新值恢复PRECHARGE并重新选择`entry=29022001`，但中途CC绕行把实际inner从240/0 ns重新缓慢拉向832/180 ns，10 s尾段Iout仍约1.33~1.35 A，最后稳定样本约Vin21.11 V、Vout28.89 V、Iout1.35 A、Pin46.9~47.5 W、Pout38.8~39.1 W；最高温度30.0 ℃、fault0，定时结束后高层停止并回到READY。

结论`FAIL_LIION_CV29_STARTUP_SPIKE_CC_DETOUR_1P35A`。该点继续锁定，不进入下一工况。根因不是832/180预充物理点本身，而是PRECHARGE→CC允许输出路径接通瞬态参与判定；下一版为两种锂电统一加入短启动稳定驻留，稳定窗口内始终保留保守预充束，窗口后仍按最新Vout持续判定，不改变已验证的LFP预充参数、CC/CV回退、AI OFF本地功能或电子负载权限边界。

### r64 v49c Li-ion CV29状态绕行已修复，电流底值仍需调参

v49c为两种锂电统一增加2500 ms启动稳定驻留；驻留期间Vpre条件仍逐样本实时更新，窗口后若最新电压仍持续高于Vpre才进入CC。生成表检查29/13/2，shared控制、energy测试与promoted-table测试均通过ARM严格编译；CPU0 text/data/bss=`179530/560/172405`，CPU1=`17558/132/9112`。双核ELF/SREC SHA256分别为CPU0 `D0DF44C3F27529D9FC8B82F164B5BBA1482C793CEAAE8D4C388A4B2C4C3335E3`/`56DBCCF72816420EDDB06892D5CD823D64BEED03E5A4FAD03A2075E0B59219E2`，CPU1 `0A8E91A49CA7BCA1FD7DD946CBFCE5A115803252132A2843EE145645502BA104`/`DBCFB2A82ACBF991FD42024B3D523A8FBD84E65D7874AA3D5FB74022B588A714`。J-Link双核烧录与校验退出码0。

在操作者保持的同一CV29工况复测12 s，启动Vout仍短暂约40.25 V，但全程保持`stage=PRECHARGE`、`entry=29022001`、TPS832/180 ns，未出现CC阶段、CC候选或EPS表项应用，说明状态修复通过。稳态板端Vin约21.10~21.12 V、Vout28.88~28.90 V、Iout1.33~1.34 A、Pin46.3~47.2 W、Pout38.5~38.7 W，最高温度29.1 ℃、fault0，结束后READY且四路功率路径关闭。电子负载全程未访问或写入。

结论`FAIL_LIION_CV29_PRECHARGE_TPS832_180_FLOOR_1P34A`：状态逻辑已闭环，但相对1 A预充目标仍偏高，原点继续锁定。下一步不改secondary或外相移，因为历史180→360 ns与-8...+16 ns探针均未给出可靠降流方向；只沿已由720→760→800→832 ns连续物理验证的primary-inner降流方向，把Li-ion专用目标小步增加到864 ns。静态边沿式`1+216+417+100=734<834`，包含400 ns死区后仍留100计数即400 ns周期余量；运行时最新边沿检查继续生效，LFP保持已通过的832/180不变。

### r65 v49d Li-ion CV29 TPS864/180电流PASS，SOFTSTART完成判据仍需修复

v49d只把Li-ion PRECHARGE primary inner从832增至864 ns，LFP保持832 ns。静态边沿式`1+216+417+100=734<834`，运行时最新边沿检查继续启用。生成表检查29/13/2与promoted-table ARM严格编译通过；CPU0 text/data/bss=`179538/560/172405`，CPU1保持`17558/132/9112`。双核ELF/SREC SHA256分别为CPU0 `8B0B8ACEED4A769B81A2E625D6D2A4155C42C284DA9F4E4A8FF455DE9AEF81A8`/`CD0428AA26D0745A3624431A63ECD32A62A8EC19DE146D07785CF9219D905C9E`，CPU1 `0A8E91A49CA7BCA1FD7DD946CBFCE5A115803252132A2843EE145645502BA104`/`DBCFB2A82ACBF991FD42024B3D523A8FBD84E65D7874AA3D5FB74022B588A714`。J-Link退出码0。

同一CV29点12 s实测全程保持电池`stage=PRECHARGE`、`entry=29022001`、TPS864/180 ns，启动Vout瞬态约39.46 V后直接回到28.92~28.97 V，没有CC或EPS绕行。稳定Iout约1.17~1.19 A、Pin42.7~43.7 W、Pout34.0~34.6 W，最高温度29.0 ℃、fault0，结束后四路路径全关。电流已达到与LFP通过点相同的约1.18 A电池友好预充范围。

但DAB外层状态12 s仍为SOFTSTART。根因是历史完成条件只允许`Vout>=0.90*Vref`转RUN；CV29预充本来就不应达到39.6 V，之前LFP/832点只是由启动高压瞬态偶然跨过该条件。结论`PASS_CURRENT_BLOCKED_BY_SOFTSTART_STATE`，仍不进入下一工况。下一版仅让已验证的自主充电束在现有3000 ms启动inner慢速窗口结束后自主转RUN；普通手动、SPS和非充电路径仍保留原0.90Vref判据，电池阶段继续按最新Vout独立判断。

### r66 v49e Li-ion CV29动态预充最终PASS

v49e只修复自主充电束的DAB启动完成条件：表项已在PWM关闭时通过完整边界检查预装，经过既有3000 ms inner慢速窗口后可自主从SOFTSTART进入RUN；普通手动、SPS及非充电路径仍要求原0.90Vref。电池阶段的2500 ms启动稳定驻留、最新Vout持续判断和Li-ion TPS864/180参数均不变。生成表检查29/13/2与promoted-table ARM严格编译通过；CPU0 text/data/bss=`179626/560/172405`，CPU1=`17558/132/9112`。双核ELF/SREC SHA256分别为CPU0 `8101F6423678408D37B33B694E286DD4B6D22D91177660130166DBBF594DD37A`/`9B145934B52594F48A25F711096FFEEB7BFF7A48EF1FCDF13BD5CBAEF3E36BF8`，CPU1 `0A8E91A49CA7BCA1FD7DD946CBFCE5A115803252132A2843EE145645502BA104`/`DBCFB2A82ACBF991FD42024B3D523A8FBD84E65D7874AA3D5FB74022B588A714`；J-Link退出码0。

同一人工CV29工况下，板端启动后约3 s自主输出`[DAB][STATE] RUN`，电池阶段全程保持PRECHARGE、entry29022001、TPS864/180 ns，没有`stage=CC`或`applied=22040009`。最后8个RUN样本平均Vin=21.0275 V、Vout=28.76375 V、Iout=1.1725 A、Pin=41.1625 W、Pout=33.7125 W；Iout范围1.15~1.26 A，其中1.26 A为RUN初期单个过渡点，随后稳定1.15~1.17 A。最高温度29.0 ℃、fault0。定时高层停止后独立回读确认v49e、READY/PWM0/RUN0、四路功率路径全关、fault0。电子负载从未访问、写入或切换。

结论`PASS_BOARD_ADC_LIION_CV29_DYNAMIC_PRECHARGE_1P17A_V49E`。锂离子低端预充从原先启动绕行和约1.35 A降到约1.17 A，并且状态完全由下位机按最新端电压自主决定。Li-ion当前锁点关闭；下一步需操作者手动把电子负载设回CV30.0 V并保持LOAD ON，对LFP已通过的832/180参数在最终v49e状态逻辑上做一次回归，仍只读板端ADC且不访问负载。

### r67 v49e LFP CV30最终固件回归PASS

操作者手动把RK8510改为CV30.0 V并保持LOAD ON后，主机完整留出10 s无仪器访问窗口；随后只通过COM4设置AI OFF、LFP12S、Imax/Ipre 1.0/1.0 A、Iterm0.3 A并发送高层start/stop。电子负载没有被访问、写入或占用。

12 s实测中电池阶段全程PRECHARGE、entry30022001、TPS832/180 ns，启动约40.28 V瞬态没有引发CC表项应用。DAB外层进入RUN；最后8个RUN样本平均Vin=21.16125 V、Vout=29.96375 V、Iout=1.18625 A、Pin=43.3625 W、Pout=35.5125 W，Iout范围1.18~1.20 A，最高温度29.0 ℃、fault0。定时高层停止后独立回读确认v49e、READY/PWM0/RUN0、四路路径全关、fault0。

结论`PASS_BOARD_ADC_LFP_CV30_DYNAMIC_PRECHARGE_1P19A_V49E`，最终固件未回归，两个化学体系的低压预充点均关闭。进入PRECHARGE→CC边界前先按已知历史风险修正调度：当前LFP在约33 V进入CC后会按最近表项选择原本43 V验证的entry416259073/EPS0/44；该表项在r54低压误用时已测得Iout升至4.13 A。为避免重复已知失败，LFP低压CC区继续复用已验证TPS832/180执行束，电池状态仍由CPU1独立显示CC；只在接近高压验证区后才允许切入正式CC表项。

### r68 v49f LFP CV33 PRECHARGE→CC状态PASS，电流趋势FAIL

v49f在41.5 V以下的LFP CC区临时复用已通过的TPS832/180 ns预充形状，用于阻止未在低压区验证的`entry=416259073` EPS0/44 ns再次误用。生成表检查29/13/2、promoted-table ARM严格编译和CPU0构建通过；CPU0 text/data/bss=`179786/560/172405`，ELF/SREC SHA256=`7448829FFAFB0BA7C77272A12FFDA22EF3F30F130E18DE5D6D06801C5F9177D1`/`2805C0FCE71F9C4B221092A9E37966652EEDA9BDDDBD39C0F55507333A8CA274`。CPU1沿用v49e镜像。双核烧录退出码0，烧录后读回版本v49f、READY/PWM0/RUN0、四路路径全关、fault0。

操作者手动设置RK8510 CV33.0 V并保持LOAD ON后，完整留出10 s无仪器访问窗口；主机仅通过COM4发送AI OFF、LFP12S和高层start/stop，没有访问、写入或占用电子负载。板端正确从`entry=30022001` PRECHARGE进入CC，末段`bat show`回报`stage=CC`；请求和有效调制均保持TPS832/180 ns，全程未出现`applied=416259073`。最后8个RUN样本平均Vin=21.145 V、Vout=32.83125 V、Iout=0.725 A、Pin=30.95 W、Pout=23.85 W，Iout范围0.72~0.73 A，最高温度28.7 ℃、fault0，结束后独立确认READY/PWM0/RUN0、路径全关。

结论`FAIL_LFP_CV33_CC_TPS832_180_CURRENT_TREND_0P725A`：状态转移和危险表项隔离通过，但从CV30预充约1.186 A进入CV33 CC后电流反而降至约0.725 A，不符合预充转正常恒流的趋势。该点继续锁定，不测下一工况。修复只沿已有物理数据方向：建立独立的低压LFP-CC表项，从TPS720/180 ns开始；该形状在CV30已安全测得1.65~1.67 A，在CV33预期可将CC恢复到约1.0~1.2 A，同时保持cap20 ns、PI2.0/0.2和0.05 A/s不变。原日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_LFP_CV33_VIN22_PRECHARGE_TO_CC_TPS832_180_v49f_r68_transition12s.log`。

### r69 v49g LFP CV33 PRECHARGE→CC修复PASS

v49g新增独立低压LFP-CC `entry=33022001`，使运行调度器在CPU1最新状态进入CC后真正把PRECHARGE TPS832/180 ns无扰切到CC TPS720/180 ns，而不是因共用`entry=30022001`而保持旧参数。cap20 ns、LFP PI2.0/0.2、slew0.05 A/s、两个预充束、Li-ion和其他功能不变。生成表检查charger/current/voltage=`29/13/2`、promoted-table ARM C11严格编译和CPU0完整构建通过；CPU0 text/data/bss=`179770/560/172405`，ELF/SREC SHA256=`F309C9477C7F8A7EC10D37268A8BED0A97F028A3F677EF7AC6ADAEBFB5FFB3CC`/`DA7197EF39F0AE005D83928340BE4D9FEA8E8AC9D5FB8795A834E2E4E2070475`；CPU1未变。J-Link烧录退出码0，CPU0 Bank0完成182272 bytes擦写校验，CPU1内容匹配。烧录后读回v49g、READY/PWM0/RUN0、路径全关、fault0，并独立加载AI OFF、LFP12、Imax/Ipre1/1 A、Iterm0.3 A。

在操作者保持的同一约22 V输入、RK8510 CV33.0 V/LOAD ON工况下复测12 s，电子负载没有被访问、写入或切换。固件先用`entry=30022001` PRECHARGE TPS832/180启动，CPU1按最新Vout进入CC后出现`candidate=33022001`，经1 s调度确认后自主`applied=33022001`；inner按已有慢变机制从832/180平滑进入720/180 ns。后6个稳态RUN样本平均Vin=21.14833 V、Vout=32.915 V、Iout=1.080 A、Pin=43.48333 W、Pout=35.56667 W，Iout范围1.07~1.09 A，最高温度28.5 ℃、fault0。未出现`applied=416259073`；定时高层停止后独立确认v49g、READY/PWM0/RUN0、四路路径全关、fault0。

结论`PASS_BOARD_ADC_LFP_CV33_PRECHARGE_TO_CC_1P08A_V49G`：相比v49f进入CC后只有0.725 A，v49g稳定到1.08 A，接近1 A本地目标且与CV30预充1.186 A处于同一友好电流平台，满足“不要求高精度，但充电趋势合适”的验收边界。原始日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_LFP_CV33_VIN22_PRECHARGE_TO_CC_TPS720_180_v49g_r69_repair12s.log`，后检`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-20_v49g_lfp_cv33_postcheck.log`。该点关闭，但整个智能充电仍需完成LFP高压CC→CV/结束段与Li-ion对应状态过渡回归后才能宣布全部完成。

### r70 上位机连续扫压暴露系统级回归，v49g结论降级并锁定修复

操作者使用串口上位机对v49g进行两种电池的连续CV扫压。上位机日志`RA8P1_DAB_UpperComputer/logs/session_20260820_120729.jsonl`表明，Li-ion10S在输出端由约44 V向29 V降低时，板端虽然最终上报`PRECHARGE`并选择`entry=29022001`，但实际inner仍由先前的`entry=22040009` EPS240/0 ns按0.04 ns/ms慢速向TPS864/180 ns移动；在28.8~28.9 V处板端Iout达到5.70~5.90 A，操作者外部显示约5 A。即状态名称已回退，执行器没有及时回到预充形状，结论`FAIL_UPPER_LIVE_SWEEP_LIION_PRECHARGE_STALE_ACTUATOR_5P90A`。

同一日志证明上位机在两种profile配置后都主动发送了`bat imax 1.000`。这覆盖了下位机按10 Ah默认容量自主生成的5 A最大充电电流，是LFP12S全程电流偏小的直接配置错误。v49g又在LFP CC且Vout<41.5 V时无条件选择`entry=33022001` TPS720/180 ns；高压启动只到约40.3 V后便进入该低功率束并停留在约39.8 V、接近0 A，无法自行到达正式高压CC/CV区，形成低功率自锁。操作者观察最大约1.3 A；板端连续记录的低压过渡峰值仍达到4.77 A，说明其PRECHARGE回切也同样受慢速执行器残留影响。结论`FAIL_UPPER_LIVE_SWEEP_LFP_IMAX1_OVERRIDE_AND_LOWCC_SELFLOCK`。

此前v49g仅在“从静止直接启动于CV33、Imax=1 A”这一窄工况通过，不能代表真实连续电压变化，也不得作为整机智能充电交付依据，`reuse_allowed=false`。修复锁定为：上位机智能充电只发送电池profile，不发送`bat vcv`或`bat imax`；下位机恢复profile默认5 A CC/1 A预充/0.3 A终止；向PRECHARGE回退时由下位机立即关闭PWM和四路功率路径、在断电状态预装预充束并自主重启；5 A常规LFP不得进入仅为1 A窄工况调出的低压CC束；表查找优先严格匹配当前充电阶段，避免CC/CV记录交叉抖动。用户于13:31:07通过上位机发送`bat stop`，随后日志确认READY、PWM=0、RUN=0、四路路径关闭、fault=0。修复期间不访问COM4、不访问或控制电子负载。

### r71 v49h连续工况修复完成构建，等待安全刷写

上位机智能充电串口映射已改为只发送`ai off`和`bat profile liion10|lfp12`，不再发送`bat vcv`与`bat imax`；界面继续只展示电池类型，启动确认明确提示充电电压、电流、状态机和调制完全由下位机决定。Python3.13上位机28项回归全部通过。

固件版本更新为`2026-08-20-dualcore-charge-live-reentry-v49h`。LFP低压TPS720/180专用CC束增加0.8~1.2 A适用条件，默认profile的5 A不会进入；充电表查找在存在当前stage记录时严格拒绝其他stage；Li-ion中段CC束不再依赖错误的1 A上位机覆盖。最关键的CC/CV→PRECHARGE回退不再带电慢慢拉inner：CPU1最新状态一旦选中预充entry且当前仍为非预充entry，CPU0立即执行PWM/RUN/四路路径全关，离线应用预充束，并自主从RESET_LOW经过原有路径时序与PWM关闭预装重新启动。该动作只由电池状态触发，不增加任何Iin/Iout软件停机阈值。

参数生成器`--check`为charger/current/voltage=`29/13/2`；MinGW C11 `-Wall -Wextra -Werror`主机执行通过promoted-table、energy state-machine、advanced-control和CAN protocol四组断言，其中新增断言确认Li-ion/LFP profile默认Imax=5 A、Ipre=1 A、Iterm=0.3 A、LFP 5 A拒绝低压1 A束、CV低压查表仍保持CV stage。CPU0完整ARM构建text/data/bss=`180426/560/172405`，ELF/SREC SHA256=`1A64B1F51E90B252871109A54589E12B4341E8A92A9E35AC7FD9A0F32639AC49`/`669B769DE0185E9D3CEA954C259A006504A42DEF00E9A9724FFEE972AA9ACFA7`；CPU1依赖构建`17558/132/9112`，哈希仍为`0A8E91A49CA7BCA1FD7DD946CBFCE5A115803252132A2843EE145645502BA104`/`DBCFB2A82ACBF991FD42024B3D523A8FBD84E65D7874AA3D5FB74022B588A714`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49h_charge_live_reentry`。当前仅完成源代码、主机测试和ARM构建，尚未刷写、尚未实物PASS；等待上位机释放COM4后再做独立停机确认与刷写，全程不访问电子负载。

### r121 v49al恢复方向通过但内部功率目标越峰，v49am离线修复完成

v49al已成功双核刷写，烧录后只读确认版本`2026-08-20-dualcore-mppt-recovery-orientation-v49al`、READY/PWM0/GPT停止、四路路径关闭、fault0。r121保持操作者给定的名义22 V/输入源限流10 A/CV36 V，只通过COM4发送AI OFF、LFP12、P&O和高层`bat start/stop`，未访问COM24。固定的主边高TPS832/180 ns欠压恢复方向有效，整轮没有重复PWM更新失败；但旧P&O仍把内部功率目标爬到360 W，在板端Vin约20.55 V、实测Pin约203 W已经出现峰值后继续加取流，最终进入约17.5 V/10.6 A、约186 W错误平台。结论`FAIL_INTERNAL_POWER_TARGET_CROSSED_SOURCE_KNEE_V49AL_R121`，停止后保持READY/PWM0，原点锁定。

操作者随后明确取消任何270~360 W或其他MPPT功率指令：输入源由操作者手动决定可用功率，板端应平滑取流、按实测`Pin=Vin*Iin`寻找峰值并承受输入；360 W只描述电路规格，不是控制目标。v49am据此删除P&O/INC内部瓦数命令和CPU0模式搜索的功率目标输入，改为0.125 A/250 ms电流探针（0.5 A/s）、实测Pin反转回退到最佳已用电流、峰值保持和持续源条件变化后自主重搜。兼容IPC字段只上传实测Pin，上位机已改标`MPPT 输入实测功率`。C11严格energy/phase主机测试通过；CPU0/CPU1完整构建通过，text/data/bss=`187402/560/172669`、`18014/132/9136`。产物、哈希与烧录脚本已固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49am_current_knee_mppt`，状态`OFFLINE_READY_AWAITING_SAME_POINT_PHYSICAL_RETEST`。

### r122 v49am消除源塌陷但过早锁峰，v49an原点修复

v49am烧录校验成功，烧录后只读确认版本正确、READY/PWM0/GPT停止、四路路径关闭、fault0。r122仍在操作者保持的名义22 V/限流10 A/CV36 V工况，只发送LFP12、P&O、AI OFF和高层start/stop，COM24未访问。运行期间板端Vin稳定在约20.89 V，未再掉入15~17 V；无PWM更新失败、fault0，证明内部瓦数指令与恢复方向问题已经关闭。但P&O在启动/执行器建立过程中被单次实测Pin下降触发，energy current reference停在约1.94 A；最终板端约Vin20.89 V、Iin6.64 A、Pin139 W，未追到10 A源折点。结论`FAIL_EARLY_PEAK_HOLD_ON_STARTUP_TRANSIENT_V49AM_R122`，停止后READY/PWM0，原点继续锁定。

v49an不改变0.125 A/250 ms即0.5 A/s探针斜率，只把越峰确认改为：连续四个250 ms样本、探针至少超过最佳电流0.25 A、Pin仍低于最佳`max(2 W,2%)`才回退保持；单次启动或模式瞬态不能锁峰。UART追踪行同时去掉360 W rating显示，只保留PinMeasured和Vknee。C11严格energy/phase测试通过；CPU0/CPU1完整构建通过，text/data/bss=`187338/560/172669`、`18102/132/9136`；镜像、哈希和烧录脚本实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49an_confirmed_current_knee`，状态`OFFLINE_READY_FOR_R122_SAME_POINT_RETEST`。

### r123 v49an找到真实峰值但低Vin误重搜，v49ao闭合峰值恢复

v49an烧录后仍在名义22 V/输入源限流10 A/CV36 V原点运行，只有COM4高层命令，COM24未访问。连续下降确认使算法不再在139 W早锁，energy reference从0.4 A按0.5 A/s爬升，板端在Vin约20.5 V处达到实测Pin峰值222.57 W并确认越峰；fault0且无PWM更新失败。但越峰时CPU1请求约8.65 A，受限源下实际Iout约4.6 A，请求与执行器存在必要的平滑回落时间。旧逻辑在5 s后把仍低于Vknee的Vin误判成源条件变化，重新从实际Iout初始化并向上爬到10 A，最后200个RUN样本平均Vin17.790 V、Iin10.65 A、Pin189.52 W、Pout167.32 W，故结论`FAIL_LOW_VIN_REACQUIRE_BEFORE_KNEE_RECOVERY_V49AN_R123`，原点继续锁定。

v49ao在peak hold期间若`Vin<Vknee-0.25 V`，只以0.005 A/10 ms即0.5 A/s下调请求直到Vin恢复，不会在低压端重新向上搜；只有Vin持续高于Vknee 0.65 V才解释为操作者提供了更高可用功率并重搜。没有新增瓦数指令或电流跳变。C11 energy/phase测试通过，CPU0/CPU1完整构建为`187330/560/172669`、`18170/132/9136`；镜像、哈希、说明和烧录脚本已实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ao_knee_voltage_recovery`。

### r124 v49ao恢复方向正确但速率慢10倍，v49ap修正实际任务周期

v49ao在同一22 V/10 A/CV36 V原点再次找到Pin峰值222.51 W、Vknee20.7 V；随后没有重新向上搜索，请求电流持续下降，fault0且无PWM更新失败。但CPU1实际每100 ms调用一次`dab_energy_update`，旧0.005 A步长只形成0.05 A/s；60 s结束时最后200点请求仍从7.48 A降到6.98 A，Vin均值17.78 V、Pin189.18 W，未及时恢复，结论`FAIL_KNEE_RECOVERY_RATE_10X_TOO_SLOW_V49AO_R124`。

v49ap仅把恢复步长修正为0.05 A/100 ms即0.5 A/s；P&O上升仍是0.125 A/250 ms，没有增加上冲。C11 energy/phase测试通过，CPU0/CPU1完整构建为`187378/560/172669`、`18170/132/9136`，镜像与哈希已固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ap_knee_recovery_rate`。

### r125 v49ap恢复电压但单向回退欠功率，v49aq改为Vknee双向保持

v49ap同一22 V/10 A/CV36 V原点末10 s已恢复到板端Vin均值21.03 V、范围20.99~21.10 V，fault0且无PWM告警，证明18 V错误平台关闭；但单向回退因执行器惯性停在energy reference3.62 A，Pin均值144.64 W，低于本轮峰值225.64 W，结论`FAIL_KNEE_RECOVERY_UNDERSHOOT_145W_V49AP_R125`。v49aq改为围绕实测Vknee的双向本地环：Vin低于Vknee减流、Vin高于Vknee加流，均为0.05 A/100 ms即0.5 A/s，0.15 V死区保持；无需功率指令或重启即可响应操作者手动改变输入源可用功率。C11 energy/phase测试和双核构建通过，镜像与哈希固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49aq_knee_voltage_hold`。

### r126 v49aq双向环无阻尼摆动，v49ar分离恢复与保持速率

v49aq同一原点峰值223.72 W，但对执行器延迟使用对称0.5 A/s调节形成大幅锯齿；末200点Vin17.80~21.24 V、Pin75.74~223.72 W、请求4.33~6.38 A，结论`FAIL_KNEE_HOLD_UNDAMPED_SAWTOOTH_V49AQ_R126`，fault0且无PWM告警。v49ar仅在首次恢复或Vin低于Vknee超过0.65 V时使用0.5 A/s；首次恢复后采用0.01 A/100 ms即0.1 A/s微调和±0.25 V死区。C11 energy/phase与双核构建通过，产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49ar_damped_knee_hold`。

### r127 v49ar稳定但死区接受低功率点，v49as收紧Vknee判据

v49ar消除大幅锯齿并连续稳定超过20 s：末400点Vin20.92~20.95 V、Pin143.47~146.26 W、energy reference恒3.63 A，fault0且无PWM告警；但实测Vknee为20.80 V，±0.25 V死区把高0.14 V的低功率点误判到位，结论`FAIL_KNEE_HOLD_DEADBAND_ACCEPTS_145W_V49AR_R127`。v49as只把阻尼保持死区收紧到±0.08 V；稳态微调0.1 A/s和严重恢复0.5 A/s不变。C11 energy/phase与双核构建通过，产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-20_v49as_tight_knee_deadband`。

### r128b v49as回到峰值但电流请求积压导致瞬时18 V，v49at加入执行器跟随门控

最初r128采集误用了不存在的`InitialCommands`参数，日志只有待机ETHDIAG且DAB从未启动，因此不计为功能测试。修正为脚本正式`Commands`参数后，r128b继续保持操作者给定的名义22 V、输入限流10 A和电子负载CV36 V；主机仅占用COM4并发送AI OFF、LFP12、P&O和高层`bat start/stop`，COM24未访问。

v49as在r128b中找到了正确输入峰值：全程峰值`Vin=20.75 V / Iin=10.76 A / Pin=223.2 W / Pout=198.8 W`，末5点重新升至`Vin=20.84→20.77 V / Pin=176.29→220.10 W`，fault0，最高主桥温度39.9 ℃。这证明收紧Vknee死区后，算法能够从低功率恢复回最大功率附近。但请求轨迹显示CPU1的MPPT输出电流请求在真实Iout约5.2 A、外相移已经480 ns饱和时仍继续积压到约8.8 A；反向后CPU0必须按0.5 A/s先卸掉数安培积压，过程中Vin最低约17.75 V。调制搜索也尚未在75 s窗口内完成最终选择，末10 s仍覆盖Vin20.77~21.24 V、Pin82.5~220.1 W，因此结论`FAIL_MPPT_REQUEST_BACKLOG_TRANSIENT_18V_V49AS_R128B`，不进入下一工况。

v49at不增加功率目标，也不加快电流斜率；只增加下位机执行器跟随门控：正向P&O/INC探针和peak-hold向上微调，必须等实际`Iout`追到当前请求的0.20 A以内；实际电流落后时保持请求，负向恢复始终允许。这样模式切换到较低传输能力时也不会继续堆积不可达电流。新增主机断言覆盖“落后时冻结、跟上后恢复0.125 A/250 ms”，energy和phase-search主机执行通过，ARM严格编译通过；双核完整构建为CPU0 `187330/560/172669`、CPU1 `18238/132/9136`。镜像、哈希和刷写脚本实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-21_v49at_actuator_follow_gate`，下一步仍只刷写并复测相同原点。

### r129 v49at消除请求积压但仍有首次CC瞬降，v49au提前识别拐点并闭合模式搜索

v49at双核刷写校验退出码0；刷写后回读版本正确、READY、CTRL OFF、PWM OFF、四路路径关闭、fault0。r129在相同名义22 V、输入限流10 A、电子负载CV36 V工况运行100 s，COM24未访问。执行器门控把MPPT请求与实际输出电流保持在约0.20 A范围内，未再出现v49as的8.8 A请求积压；最终10 s稳定为Vin均值20.942 V、范围20.920~20.962 V，Iin均值9.815 A、范围9.643~10.051 A，Pin均值205.54 W、范围201.83~210.40 W，Pout均值183.82 W，最高主桥温度44.1 ℃，fault0。搜索峰值为Vin20.864 V、Iin10.672 A、Pin222.66 W。

但首次跨越台式电源CC折点时，旧连续四点确认要到Vin17.81 V附近才进入peak hold，全程最低Vin17.783 V；虽然此后自主恢复且不再停在低压端，仍不满足“输入保持约22 V、不得掉到18 V”的硬要求。另一个确定缺陷是CPU0模式搜索要求`target_reversed`和`current_settled`在同一次更新同时成立：CPU1的100 ms目标变化被观察到时CPU0主动参考尚未追上，追上后反转脉冲已经消失，因此100 s内始终停在phase stage1，未进行SPS/EPS/TPS最终排序。结论`FAIL_INITIAL_CC_DIP_AND_PHASE_REVERSAL_RACE_V49AT_R129`，原点继续锁定。

v49au在P&O学习瞬态峰值之前检测实测轨迹拐弯：若一个MPPT间隔内Vin下降超过0.12 V、Iin未下降且Pin仍不低于历史最佳90%，立即进入peak hold，并经既有0.5 A/s斜率施加10%电流余量，余量限制为0.125~0.375 A；深跌四点确认保留作后备。CPU0新增反转事件锁存，等主动电流参考真正稳定后再离开WAIT并开始三模式比较。两项新增主机断言、完整energy/phase测试、ARM严格编译和双核构建均通过，镜像实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-21_v49au_early_knee_phase_latch`，下一步仍只复测相同原点。

### r130 v49au模式搜索闭合且不再跌至18 V，但自然过渡误判拐点；v49av加入已跟随门禁

v49au已双核刷写并在相同名义22 V、输入限流10 A、电子负载CV36 V工况复测；只通过COM4发送AI OFF、LFP12、P&O和高层`bat start/stop`，COM24未访问。提前拐点检测把整轮Vin最低值抬到20.514 V，首次跨源CC不再掉到18 V；CPU0反转锁存也使SPS/EPS/TPS搜索首次完整结束，最终报告`mode=EPS outer=8 ns inner=13/0 ns PinBest=142.73 W etaBest=90.611% complete=1`，证明模式搜索逻辑缺口关闭。

但本轮最终只到约Vin20.52 V、Iin6.86 A、Pin143.7 W，明显没有追到已知约205~223 W区域。日志定位为早期自然传输/调制过渡期间，实际Iout约3.50 A而MPPT请求仅约2.34 A，执行器尚未跟随；这段瞬态Vin弯折被v49au误判成电源拐点并提前锁峰。结论`FAIL_FALSE_EARLY_KNEE_WHILE_ACTUATOR_UNSETTLED_V49AU_R130`，按硬约束停止且不进入下一工况；结束脚本已发送高层`bat stop`并释放COM4。

v49av只收紧提前拐点资格：实际Iout必须与当前MPPT请求相差不超过0.20 A，才允许根据Vin弯折锁峰。r130的约1.16 A偏差会被拒绝，而r129真实源拐点时请求与实际Iout约0.17 A仍能通过。新增反例断言、完整energy/phase主机测试和ARM C11严格编译通过；双核完整构建为CPU0 `187410/560/172669`、CPU1 `18442/132/9136`，镜像、哈希与刷写脚本已实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-21_v49av_tracked_early_knee`。下一步只刷写并复测同一22 V/10 A/CV36 V点。

### r131 v49av找到225 W并完成排序，但同速回退保留滞后且重启搜索；v49ax闭合回退与回放

v49av双核刷写校验退出码0，烧录后回读版本正确、READY、PWM/GPT停止、四路路径关闭、fault0。r131继续保持操作者的名义22 V、输入限流10 A、电子负载CV36 V，只经COM4发送AI OFF、LFP12、P&O和高层start/stop，COM24未访问。早期未跟随瞬态不再假锁峰，实测DAB峰值`Vin20.954 V/Iin10.663 A/Iout5.511 A/Pin223.44 W/Pout198.93 W`；三模式搜索完整结束并中选`SPS outer428 ns, PinBest225.07 W, eta89.658%`，fault0。

全量日志同时暴露两个未关闭问题。拐点反转后CPU1目标先从5.65 A降到5.28 A，CPU0实际参考仍约5.63 A；随后两者都按0.5 A/s下降，初始约0.35 A滞后无法闭合，源端继续CC并使Vin最低到17.853 V。模式中选回放时CPU1请求已被拖到约3.88 A，Pin只有约154~160 W，低于中选225 W的80%，旧逻辑又在约19 s后重启搜索。结论`FAIL_COUPLED_BACKOFF_LAG_AND_LOW_POWER_REPLAY_V49AV_R131`，85 s超时脚本发送高层`bat stop`并释放COM4，原点继续锁定。

v49aw先增加“Vin仍在拐点安全带内且Pin低于本轮实测最佳90%”时的0.01 A/100 ms回放辅助；它不是固定瓦数命令，Vin下跌始终优先回退。进一步复核r131后未刷写v49aw，而直接合并为v49ax：每次目标负向回退后，必须等待实际Iout跟到目标+0.05 A以内才允许下一步，CPU0物理参考仍按原0.5 A/s下降，因此不会增加MOS电流变化率，且可消除目标与执行器同速造成的永久滞后。新增断言覆盖未跟随冻结、跟随后单步0.05 A、低功率平滑回放和源跌落优先；完整energy/phase主机测试、ARM严格编译及双核构建通过。CPU0/CPU1为`187370/560/172669`、`18550/132/9136`，产物和哈希实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-21_v49ax_coupled_backoff_replay`。

### r132 v49ax回退滞后改善但大功率上探仍过快；v49ay改为150 W以上0.1 A/s探测

v49ax双核刷写校验成功后，同一名义22 V/限流10 A/CV36 V原点仅用COM4高层命令复测，COM24未访问。执行器耦合回退使最低Vin由r131的17.853 V改善到18.641 V，并自主恢复至约Vin20.94 V、Pin237.9 W；全程fault0。峰值Pin239.7 W后为遵守功率边界创建停止信号，脚本完成10 s观察后发送高层`bat stop`并释放COM4。由于Vin仍进入18 V区，结论`FAIL_HIGH_POWER_PROBE_CROSSES_KNEE_TOO_FAST_V49AX_R132`，原点继续锁定。

v49ay不提高任何功率或电流边界：150 W以下仍0.125 A/250 ms，150 W以上正向探测降为0.025 A/250 ms即0.1 A/s；负向恢复保持最大0.5 A/s。高功率拐点的单周期Vin弯折阈值从0.12 V提前到0.04 V，并继续要求实际Iout跟随请求，避免自然过渡误判。新增高功率斜率断言、完整energy/phase测试、ARM严格编译和双核构建通过，CPU0/CPU1为`187370/560/172669`、`18658/132/9136`，产物已实时固化。

### r133 v49ay大功率P&O本体通过但模式试探仍触碰18 V；v49az加入本轮动态Vin底线

v49ay同一22 V/10 A/CV36 V原点中，150 W以上0.1 A/s探测使首次接近MPP阶段Vin最低20.525 V，Pin峰值240.01 W；证明P&O大功率探测与拐点反转本体不再把输入拉到18 V。三模式搜索完成并中选`SPS outer436 ns, PinBest239.23 W, eta89.232%`，中选后末200点Pin均值约233.73 W、Vin约20.69 V、fault0且没有重复重搜。

但全量日志显示SPS候选试探期间旧逻辑只在低于17.5 V时退出；该候选曾把Vin拉到17.997 V约数秒，随后才恢复。即最终结果与重复搜索均关闭，切换瞬态仍违反“不得掉到18 V”硬要求，故记`FAIL_MODE_TRIAL_USED_GLOBAL_FLOOR_V49AY_R133`，原点继续锁定。v49az在本轮实测源拐点处记录动态底线：任一模式低于拐点0.5 V即平滑退出并标记不适用，恢复到动态底线上方0.25 V后才试下一模式；名义20 V工况的17.5 V全局下限不变。主机energy/phase、ARM严格编译和双核构建通过，CPU0/CPU1为`187442/560/172673`、`18658/132/9136`，产物已实时固化。

### r134 v49az动态底线锚点滞后失败；v49ba改用最高Pin配对Vin

v49az同点复测最低Vin18.416 V、峰值Pin223.74 W、fault0，动态底线虽触发，但其锚点取自反转确认时已经下降的Vin，因此只把退出点锁在约18.4 V；结论`FAIL_DYNAMIC_FLOOR_ANCHORED_TO_LATE_REVERSAL_V49AZ_R134`。v49ba在整轮搜索中同步保存最高实测Pin对应Vin，并以该Vin减0.25 V作为模式候选底线；恢复到最高Pin电压区域后才继续下一候选。energy/phase主机测试与ARM构建通过，CPU0/CPU1为`187698/560/172677`、`18658/132/9136`，产物及哈希实时固化。

### r135 v49ba模式搜索安全完成但回放只到90%；v49bb提高实测峰值回放比例

v49ba同一22 V/10 A/CV36 V原点全程Vin为20.666~21.673 V、Pin峰值222.94 W、Iin峰值10.701 A、Iout峰值5.495 A、Tpri峰值50.06 C、fault0；三模式自主搜索完成一次并中选`EPS outer480 ns / primary inner89 ns / secondary inner0 ns`，相位搜索记录`PinBest=225.68 W, etaBest=89.421%`，之后无重复重搜。安全底线与模式搜索通过。

但110 s超时前正常运行段只稳定约Pin189~190 W、Vin约21.0 V、Iin约9.05 A、Iout约4.69 A，未回到本轮已证明的最大功率区域，故记`FAIL_REPLAY_STOPS_AT_90_PERCENT_V49BA_R135`，同点继续锁定。原因是能量层以自身较低采样频率记录的约211 W最佳值乘90%判定回放完成，恰好在约190 W停止；相位层225.68 W峰值没有被该判据直接使用。

v49bb不增加功率/电流上限、不加入瓦数命令，只把能量层本轮实测最佳的回放完成比例从90%提高到97%，仍以0.01 A/100 ms恢复；Vin低于实测拐点安全带时原有退流逻辑优先。新增95%仍恢复、98%停止以及Vin下探优先退流断言，energy/phase主机测试、ARM严格编译与双核构建通过，CPU0/CPU1为`187698/560/172677`、`18658/132/9136`；产物及SHA256已固化于`artifacts/2026-08-21_v49bb_live_mpp_replay_97`。

### r136 v49bb恢复波形过渡失败；v49bc顺序斜坡消除136/136 ns中间态

v49bb原点复测峰值Pin223.91 W、Iin10.83 A、Iout5.54 A、fault0，但峰值后从SPS切向恢复TPS832/180 ns时，两路inner按0.2 ns/ms从0同步上升，实测经过约136/136 ns；该中间态未能卸载，Vin从拐点约20.9 V滑到18.368 V。立即高层`bat stop`，结论`FAIL_RECOVERY_INNERS_SLEW_TOGETHER_V49BB_R136`。

v49bc只修复此过渡：primary inner先连续斜升到832 ns且secondary固定0 ns；primary实际到位后secondary才连续斜升到180 ns，两路实际到位前禁止进入下一模式。回归覆盖400/0保持WAIT、832/0释放secondary、832/180才进入下一候选；energy/phase、ARM严格编译和双核构建通过，CPU0/CPU1为`187722/560/172677`、`18658/132/9136`，产物实时固化于`artifacts/2026-08-21_v49bc_sequenced_recovery_inner`。

### r137 v49bc消除18 V下跌但TPS重复；v49bd兼容收口

r137同点峰值Pin223.67 W、Iin10.73 A、Iout5.50 A、Tpri41.71 C、fault0，Vin全程20.84~21.39 V；恢复日志严格呈现832/0到832/180 ns顺序斜坡，证明r136危险中间态已消除。但TPS候选跨动态底线后没有不可行标志，恢复后再次进入TPS，100 s超时仍未完成选择，结论`FAIL_TPS_RETRY_LOOP_V49BC_R137`。

为避免影响历史成功点，v49bd仅在最高Pin>=210 W且其配对Vin为20.3~21.5 V时启用顺序恢复；其他已委任低/中功率路径保持旧行为。新增TPS不可行标志，三模式均跨底线后直接选本轮已记录最佳样本。promoted table、energy、phase、ARM严格编译与双核构建通过，产物固化于`artifacts/2026-08-21_v49bd_scoped_recovery_no_tps_retry`。

### r138 v49bd退出判断顺序错误；v49be只提高TPS失败终结优先级

r138 Vin20.65~21.39 V、峰值Pin222.92 W、Iin10.70 A、Iout5.50 A、Tpri40.78 C、fault0，但TPS跨底线回到WAIT后先命中通用SPS可用分支，仍重跑SPS，故提前高层停止并记`FAIL_TPS_FINALIZE_PRIORITY_V49BD_R138`。v49be只把TPS失败后的已有结果择优放在通用重启判断之前；成功候选、历史参数表及低/中功率路径不进入此分支。energy/phase/promoted-table测试与双核构建通过，产物固化于`artifacts/2026-08-21_v49be_finalize_after_tps_floor`。

### r139 v49be当前锁点通过

v49be在相同名义22 V、源限流10 A、电子负载CV36 V点只接收AI OFF、LFP12、P&O和高层`bat start/stop`。三模式一次完成并选中`SPS outer424 ns / inner0/0 ns`，搜索记录`PinBest=224.51 W, etaBest=89.290%`，之后无重搜；搜索全过程Pin峰值223.22 W、Iin10.82 A、Iout5.53 A、Vin最低19.12 V，所有变化保持既有current/inner斜率且fault0。

完成后连续稳定16.562 s；末10 s共134点：Vin20.908 V（20.890~20.922 V）、Pin218.86 W（216.69~220.96 W）、Pout193.791 W、Iin10.468 A、Iout5.389 A、Tpri最高47.55 C、Tsec最高33.43 C，fault0。结论`PASS_MPPT_22V10A_CV36_FINALIZE_AFTER_TPS_FLOOR_V49BE_R139`。超时只发送高层`bat stop`并释放COM4，COM24未访问；下一项为运行中人工改变源限流，验证自主再追踪与稳定保持。

### r140 运行中源限流10 A改7 A通过

v49be先在22 V/10 A/CV36 V通过高层命令自主完成搜索并选中`SPS outer432 ns / inner0/0 ns`，记录`PinBest=225.39 W, etaBest=89.275%`。保持PWM和功率传输连续时，操作者仅将输入源限流从10 A人工改为7.0 A；主机未发送任何运行时命令且未访问COM24。

人工调整后末10 s共134点：Vin20.934 V（20.752~21.068 V）、Iin7.173 A（7.038~7.255 A）、Pin150.163 W（148.21~152.05 W）、Pout135.204 W、Iout3.755 A、Tpri最高54.22 C、Tsec最高35.81 C、fault0。下位机保留SPS并把实际有效相移连续降至约14~16 ns，不停机、不硬切换、不重复整轮搜索即贴住新的源限流边界。结论`PASS_MPPT_LIVE_ILIM_10A_TO_7A_V49BE_R140`；高层停止后释放COM4，下一步做当前固件的串口上位机代表点联调。

### r141 串口上位机代表点完整通过

使用当前`RA8P1_DAB_UpperComputer`新进程连接COM4，JSONL为`logs/session_20260821_012612.jsonl`。连接和配置阶段总计只发送11条命令：`version`、`alglog on`、`mode save`、`show`、`ai off`、`bat profile lfp12`、`mppt po`和高层`bat start`；没有固定功率、相移、inner、cap、energy模式或其他运行时底层命令。COM24未访问，电子负载仍由操作者独占。

下位机独立进入`SOLAR_CHARGER / LIFEPO4_12S_43P8V / CONSTANT_CURRENT`，AI OFF保持有效。01:26:50首次完成三模式搜索，最终选择`EPS outer480 ns / primary inner141 ns / secondary inner0 ns`，记录`PinBest=154.66 W / etaBest=90.987% / complete=1 / local=1`。完成后持续RUN；01:28:33~01:28:43连续10 s共207点：Vin20.890 V（20.576~21.087 V）、Iin7.132 A（6.857~7.322 A）、Pin148.978 W（144.57~152.90 W）、Vout35.969 V、Iout3.709 A、Pout133.395 W、ADC效率89.541%（88.653~90.501%）、Tpri最高56.81 C、Tsec最高36.77 C。全窗状态均为RUN、PWM ON、EPS、fault0、valid，AI应用次数0。

01:29:22操作者在上位机点击停止，JSONL只新增高层`bat stop`，功率会话结束。结论`PASS_UPPER_FUSED_CHARGE_MPPT_V49BE_R141`：功率建立、板端自主搜索、AI OFF、高层命令边界、10 s保持和上位机停止均通过。

### r142 LFP完整充电轨迹进行中

r141停止后，操作者将电子负载置于低压CV点，并通过同一上位机只配置AI OFF、LFP12、P&O和高层`bat start`。下位机从01:31:19到01:31:39连续20 s保持`PRECHARGE`，能量参考1.00 A；代表帧为板端Vout31.078 V、Iout1.010 A、Vin21.190 V、Pin38.55 W、fault0、AI未应用。该证据确认低压预充真实由下位机状态机控制。

操作者继续平滑提高CV电压，Vout越过LFP的32.4 V门槛后，下位机自动进入`CONSTANT_CURRENT`并启用MPPT；没有上位机底层命令。三模式搜索在01:32:07完成并选中`EPS outer140 ns / primary inner280 ns / secondary inner0 ns`，记录`PinBest=151.83 W / etaBest=90.521% / complete=1 / local=1`，全程fault0。

全量审阅`logs/session_20260821_012612.jsonl`后，r142不能按“无fault”判通过。01:31:39~01:38:05的CC段总计完成7轮全模式搜索，完成时间依次为01:32:07、01:33:01、01:34:22、01:34:58、01:35:55、01:37:19和01:38:05；7轮最终都选择EPS，却伴随Iout接近0~4.19 A反复摆动、Vin最低17.548 V，并在01:35:02和01:37:20出现`PWM update missed >100 times`。根因是HOLD条件将正常充电中的`|Vout-anchor|>1 V`或`Pin变化>35%`直接视为新工况，操作者连续提高CV电压时约每1 V就触发一次完整SPS/EPS/TPS扫描。结论`FAIL_CC_REPEATED_MODE_RESEARCH_V49BE_R142`，原31→45 V轨迹锁定。

CV在约Vout43.54 V正确进入，Iout从约3.69 A随Vout升高连续下降到接近0 A，趋势通过；但入口约3 s内出现EPS→TPS→EPS双切换。操作者继续升到45 V后停止，没有在LFP 43.8 V附近的有效终止窗口保持30 s，故DONE本轮未验证，不能记完整充电PASS。

v49bf最小修复已落盘：正常Vout轨迹和输入限流导致的Pin变化不再触发全模式重搜；只有Vin相对锚点真实变化超过1 V、持续5 s、实际电流已跟随且Pin仍不低于锚点80%时才允许重新比较。充电运行时模式候选确认由1 s延长至3 s，以抑制CV入口瞬态TPS候选。电流/功率斜率、已有r139/r140成功参数、保护边界和AI OFF确定性路径均未放宽。下一步为主机回归、ARM双核构建、固化v49bf后，在同一31→45 V轨迹复测。

### 本轮交付范围收敛：CP取消

CP能源路由不能用少量点完成可信验收，至少仍需恒功率负载稳定性、20/22/24 V输入扰动、功率边界、阶跃恢复和三模式选择。本轮时间不足，故状态记为`CANCELLED_OUT_OF_SCOPE_2026-08-21`：从上位机删除CP入口与CPD显示，不执行CP实测，不宣称交付；固件内部兼容代码保留，避免影响已成功的智能充电+MPPT路径。

离线门禁结果：上位机29/29回归通过，界面只暴露“智能充电+MPPT”；phase-search测试翻译单元经Renesas ARM C11 `-Wall -Wextra -Werror`严格语法编译通过。当前Windows环境缺少可执行主机C测试的编译器，故不把语法检查冒充主机断言PASS。正式参数生成器`--check`仍为charger/current/voltage=`29/13/2`。CPU0/CPU1完整构建退出码0，text/data/bss分别为`187962/560/172681`和`18658/132/9136`；镜像、哈希和刷写脚本已实时固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-21_v49bf_continuous_charge_hold`。当前状态`BUILT_NOT_FLASHED_AWAITING_SAME_TRAJECTORY_RETEST`。

操作者明确最终展示将从31.0 V按0.1 V步进上升，并要求稳定优先、在不破坏稳定的前提下尽快找到MPP。展示门禁补充为：普通步进至少保持1 s，32.3/32.4/32.5 V和43.4~43.9 V阈值窗口至少保持2 s；LFP在32.4 V持续500 ms且启动已稳定后进入CC+MPPT，接近43.8 V进入CV减流。v49bf保留150 W以下最大0.5 A/s、150 W以上0.1 A/s的已验证探测速度，不为缩短时间重新提高斜率；加速来自消除重复全模式搜索和复用已验证参数，而不是加大电流冲击。上位机新增两种电池的阶段阈值提示，但仍不允许人工设置目标电压、电流或底层调制。

上位机增加阈值提示后的回归为30/30 PASS；未改动的energy状态机既有主机可执行测试仍全部通过。v49bf随后经J-Link连接`R7KA8P1KF_CPU1`完成双核升级，CPU0 Bank0 `190464 bytes`擦除、编程、校验完成，CPU1内容一致，退出码0；原始日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-21_v49bf_continuous_charge_hold_dualcore_flash.log`。刷写后仅短时占用COM4发送只读`version/show/bat show`并立即释放，确认版本`2026-08-21-dualcore-continuous-charge-hold-v49bf`、READY、CTRL OFF、PWM0、GPT RUN0、四路路径全关、fault0、LFP `Vpre32.40/Vcv43.80 V`、AI deterministic fallback LOCAL/OFF；日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-21_v49bf_postflash_queryonly.log`。电子负载未访问。当前状态`FLASHED_READY_AWAITING_R143_LFP_0P1V_TRAJECTORY`；为优先稳妥，首轮固定输入源22.0 V/7.0 A限流，电子负载由操作者从CV31.0 V起步。

### r143 v49bf连续轨迹失败：禁止重搜与固定outer cap共同破坏MPPT

上位机JSONL `RA8P1_DAB_UpperComputer/logs/session_20260821_020019.jsonl`完整记录了操作者从LFP低压预充向45 V连续升压的过程。电子负载由操作者独占，主机没有访问或写入COM24。低压约Vout30.73 V时PRECHARGE/TPS832/180 ns稳定约1.00 A；越过32.4 V进入CC后，融合入口仍请求SPS，使Iout在短时间内升到约4.9~5.1 A。首次模式比较又使Iout由约4.7 A下降到约2.5 A，并出现一次`PWM update missed >100 times`累计告警。

约02:02:12三模式完成并选择`EPS outer172 ns / inner45/0 ns`，记录`PinBest189.64 W / eta89.992%`。选中后Vout由约34.7 V继续升高；虽然CPU1能量电流参考始终约4.92 A，CPU0把闭环外相移上限永久锁在172 ns，导致Iout由约4.8 A连续下降到Vout42.4 V附近约1.33 A，Pin由约188 W降到约61 W，P&O无法继续驱动执行器。约Vout43.84 V进入CV后EPS保持，Iout按趋势由约1.02 A降到接近0 A，说明3 s候选确认和CV段本身有效。全程fault0，最终由上位机高层停止并回到功率关闭状态。

结论`FAIL_MPPT_LIVE_TRAJECTORY_FIXED_REPLAY_CAP_AND_ENTRY_MODE_STEP_V49BF_R143`。用户指出“禁止重搜毁了功能”正确；修复不能回到r142每约1 V重新执行低功率全模式扫描，而要把连续P&O电流追踪与有界模式效率比较分层。

### v49bg连续MPPT与局部重搜离线修复完成

固件版本更新为`2026-08-21-dualcore-continuous-mppt-research-v49bg`。融合CC不再从SPS硬起步：调度束改为已验证TPS832/180 ns，phase-search首次进入时继承板上实际requested/effective模式、outer和inner，不改变CPU1正在使用的P&O电流参考。继承波形若外相移已饱和且实测Iout仍比请求低0.20 A以上，才每1 s减少32 ns inner；底层仍通过既有0.2 ns/ms slew执行，因此不会形成一步硬切。

正常P&O电流反转已经代表到达当前源MPP，不再先切到TPS832/180低功率恢复波形；只有Vin真实越过动态/全局底线时保留r136已验证的顺序TPS卸载。模式中选后，最佳outer只形成`best outer + 16 ns`初始cap；若后续升高电池电压造成外环饱和且Iout落后请求0.15 A以上，则每250 ms只放开8 ns，直至现有480 ns绝对相移上限。若480 ns仍持续5 s无法跟流，必须围绕当前参考局部重搜，不能低功率锁死。

工况重判恢复为分层策略：普通0.1 V电池步进由连续P&O/电流环直接处理；Vin相对锚点变化超过1 V、Vout变化超过4 V、可用Pin变化超过20%且电流已经跟随，或480 ns控制余量已耗尽，持续5 s后触发局部SPS/EPS/TPS效率重排。局部重排继承现有模式、inner和电流参考，不清零功率、不使用固定瓦数指令、不依赖上位机底层命令。AI OFF仍完整工作。

离线门禁：更新的phase测试源经Renesas ARM C11 `-Wall -Wextra -Werror`严格编译通过；当前Windows仍无可执行本机C编译器，因此不把phase断言标为已执行。未改动的energy主机断言和promoted-table断言执行通过；参数生成器`--check`为charger/current/voltage=`29/13/2`；上位机30/30通过。CPU0完整构建text/data/bss=`188850/560/172697`，CPU1=`18658/132/9136`。CPU0 ELF/SREC SHA256=`155B5E30FFF0075DF3DB605979AC3B56AFD06EBD88B8DF39D375EFD4A2C440F7`/`9A8FF2EF1E3D6217009F8BB924E483085F60E52365DB0F25EEE6AD92F3C018E4`；CPU1=`3568EEE0B4666968A11F12BF60618D86EC79F357FE5654D973D321692FC58945`/`0D89F3ADFB83E54459BDEAC34FFF979727443E4B4007CA4A3312A6220429E25E`。产物位于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-21_v49bg_continuous_mppt_research`。当前`OFFLINE_BUILT_NOT_FLASHED_AWAITING_R143_SAME_TRAJECTORY_RETEST`，尚未宣称实物PASS，也未占用COM4或电子负载。

v49bg随后通过J-Link/SWD连接`R7KA8P1KF_CPU1`完成双核刷写和校验：CPU0 Bank0编程并校验`190464 bytes`，CPU1镜像内容匹配，退出码0。刷写后只短时打开COM4发送`version/show/bat show`查询并立即释放；回读版本`2026-08-21-dualcore-continuous-mppt-research-v49bg`，状态`READY / CTRL OFF / PWM=0 / GPT_RUN=0`，PRE/MAIN/OUT/RESET_N/RUN_REQ全为0，fault0，板端Vin23.30 V、Vout0.10 V、Pin约0 W，LFP阈值Vpre32.40/Vcv43.80 V，AI确定性回退LOCAL。原始日志为`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-21_v49bg_continuous_mppt_research_dualcore_flash.log`和`2026-08-21_v49bg_postflash_queryonly.log`。COM24未访问。当前`FLASHED_READY_AWAITING_R143_SAME_TRAJECTORY_RETEST`。

### r144 v49bg名义24 V连续CV轨迹失败，v49bh离线修复完成

操作者把输入源设为名义24 V并通过串口上位机连续提高LFP电子负载CV；主机只读`RA8P1_DAB_UpperComputer/logs/session_20260821_024042.jsonl`，没有打开COM4或访问COM24。约02:41:40三模式比较选中`EPS outer196 ns / cap212 ns / inner390/0 ns`，记录PinBest173.92 W、ADC效率88.584%。随后Vout约33.9→40.5 V时Vin稳定约22.9~23.0 V，无fault，但Iin约7.03→3.41 A、Iout约4.22→1.74 A、Pin约161→78 W；直到约Vout41.4 V才开始放开cap。结论`FAIL_MPPT_CV_TRAJECTORY_STALE_1P79A_REFERENCE_AND_LATE_RETUNE_V49BG_R144`。

根因不是输入源缺功率，而是两层执行器失配：EPS390/0 ns本身在outer=0时仍物理传输4~5 A，CPU1的energy current request却一直停在1.79 A；CPU0外相移PI因此长期压在0，`Iref>Iout+0.15 A`永远不成立，v49bg的cap释放逻辑实际不可达。HOLD又使用10 s grace、4 V条件和5 s确认，远慢于操作者0.1 V步进。

v49bh加入一次性无扰执行器交接：峰值保持中的中选波形若已经复现至少90%本轮MPP、实际Iout又高于旧请求0.25 A以上，CPU1把逻辑目标重基准到已经流过的Iout；CPU0 active reference仍沿用不超过0.5 A/s的物理斜率，故不会因目标重基准产生新的电流跃变。P&O默认改为0.1 V/250 ms持续小步寻优；每次实际Vout变化达到0.08 V，若升压同时Pin下降超过max(2 W,1%)或出现电流缺口，则只把外相移cap放开8 ns，保持当前模式和inner。完整SPS/EPS/TPS效率复核改为累计约2 V或Pin约10%变化、持续1 s才触发，局部搜索仍继承实时波形和电流，不清零功率。

旧经验没有覆盖：29组充电、13组电流和2组电压生成参数表校验不变；r111~r116模式记录原样保留，新增r139的SPS 22 V/10 A/CV36和r141的EPS 22 V/7 A/CV36硬件PASS点作为AI-OFF等价结果择优先验，仍不作为固定相移指令。新增主机断言直接复现1.79 A→4.60 A中选波形地板交接，并验证下一次真实Vin下降仍只做0.01 A/100 ms温和回退；另一个断言验证Vout33.0→33.1 V、Pin170→166 W时cap只增加8 ns且模式仍为EPS。

离线门禁全部通过：MinGW本机执行energy、phase、promoted tables、advanced control、CAN protocol五组严格C11测试；参数生成器`--check`仍为charger/current/voltage=`29/13/2`；上位机30/30。CPU0完整构建text/data/bss=`189162/560/172709`，CPU1=`18802/132/9140`。CPU0 ELF/SREC SHA256=`F45E69715938E943E01FB6225C705EFB2F03D157BBBD8B3055C491551FB04241`/`7A4B6F47A36EB5552D58ECD1884AD558B98679F9FA3AAC130136738999A05D44`；CPU1=`1E838F48936A41F24D42F90176F95DBBCCE0AA7B1BEC2A366B680F09491D2910`/`8764CF95D3F1DE59563E8A6D14B8E337B557703875C5FC4E54A4A9BC493BAB33`。产物固化于`RA8P1_DAB_Feature_Showcase/dual_core/artifacts/2026-08-21_v49bh_cv_step_continuous_mppt`；当前`OFFLINE_BUILT_NOT_FLASHED_AWAITING_R144_SAME_TRAJECTORY_RETEST`。

v49bh随后经J-Link/SWD连接`R7KA8P1KF_CPU1`完成双核刷写与校验：CPU0 Bank0实际编程/校验190464 bytes，CPU1实际编程/校验20480 bytes，退出码0。刷写后短时打开COM4只发送`version/show/bat show/ai show`，确认版本`2026-08-21-dualcore-cv-step-mppt-v49bh`、READY、CTRL OFF、PWM0、PRE/MAIN/OUT/RESET_N/RUN_REQ全0、fault0、P&O默认step0.10 V/interval250 ms、LFP Vpre32.40 V/Vcv43.80 V、AI fallback LOCAL；随即释放COM4，COM24未访问。刷写日志`DAB_RA8P1_PCB_Notes/serial_logs/2026-08-21_v49bh_cv_step_continuous_mppt_dualcore_flash.log`，回读日志`2026-08-21_v49bh_postflash_queryonly.log`。当前`FLASHED_READY_AWAITING_R144_SAME_TRAJECTORY_RETEST`。

### r145 v49bh 0.1 V轨迹仍退功率；v49bi恢复MPPT闭环所有权

只读审阅`RA8P1_DAB_UpperComputer/logs/session_20260821_025831.jsonl`。有效RUN为02:58:39.526~03:02:42.898，4815个关联遥测点。上位机TX只有连接查询、`ai off`、`bat profile lfp12`、`mppt po`、`bat start`和`bat stop`，排除主机固定功率或底层相移干预。全过程fault0，峰值Pin208.1 W、Iin9.12 A、最高温度64.0 C。

按0.1 V聚合后，Vout32.8 V时Iin7.49 A/Pin171.4 W；35.6 V时TPS达到Iin8.85 A/Pin203.0 W；继续升压到36.8 V却掉到6.24 A/143.4 W；第二轮搜索在37.1 V重新达到8.71 A/199.8 W，随后又降到41.3 V的5.41 A/124.6 W和42.4 V的4.93 A/113.5 W。Vout43.5 V附近进入CV后减流趋势另行有效，不属于本次MPPT失败。

日志显示CPU1 energy Iref在前半段锁为2.12 A、后半段仅2.42~2.44 A，而实际EPS/TPS Iout多次为4~5 A；outer saturation长期为真。35.6 V中选TPS outer48 ns/cap64 ns/inner190/96 ns后，Vout逐步升高时程序只把cap放到104 ns，实际outer仍48 ns，故所谓轻量retune没有改变执行器。结论`FAIL_MPPT_FINDS_8P5A_BUT_LOSES_IT_V49BH_R145`。

用户补充本次物理验收：CC/MPPT区Vin约22~23 V时Iin应持续不低于8.5 A；硬件已两次达到该点，后续掉点即失败。该数值不固化为控制指令，板端仍以实时`Vin*Iin`自主寻找源边界；进入LFP CV阶段后由充电状态机减流。

v49bi修复两层所有权。CPU1在任一模式中选后，只要Vin不低于本轮最佳电压0.20 V以上、实际Iout已经高于旧请求0.25 A且Pin不低于20 W，就把MPPT逻辑参考重基准到已经流过的电流；每次后续中选均可再次执行，真实Vin下探时禁止。CPU0只在CC/MPPT、phase HOLD且CPU1目标与ADC实际相差不超过0.25 A时，把active reference无扰重基准到该现有电流；物理电流不跳变，outer PI立即获得升降双向控制权。

离线结果：energy与phase新增断言执行通过，promoted tables、advanced control和CAN host测试通过；上位机30/30；参数表charger/current/voltage=`29/13/2`。第一次RA构建因本文件不存在`dab_absf`按失败即停规则终止，改用既有`dab_measurements_abs`后CPU0/CPU1完整构建通过，大小`189522/560/172709`与`18834/132/9140`。产物和哈希在`artifacts/2026-08-21_v49bi_mppt_track_hold`，状态`OFFLINE_BUILT_NOT_FLASHED_AWAITING_R145_SAME_TRAJECTORY_RETEST`。
