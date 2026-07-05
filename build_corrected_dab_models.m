function build_corrected_dab_models()
% Corrected realistic DAB switching models with proper MOSFET bridge polarity.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
if ~exist(notesDir, 'dir'), mkdir(notesDir); end

build_one('DAB_RA8P1_Final_Physical_MOSFET', notesDir, false, false);

disp('FINAL_PHYSICAL_MOSFET_DAB_MODEL_BUILT');
end

function build_one(mdl, notesDir, secGan, mpptMode)
dst = fullfile(notesDir, [mdl '.slx']);
if bdIsLoaded(mdl), close_system(mdl, 0); end
if exist(dst, 'file'), delete(dst); end
new_system(mdl); open_system(mdl);
stopTime = '0.00045';
set_param(mdl, ...
    'StopTime',stopTime, ...
    'SolverType','Variable-step', ...
    'Solver','daessc', ...
    'MaxStep','5e-8', ...
    'RelTol','2e-3', ...
    'AbsTol','1e-5', ...
    'ReturnWorkspaceOutputs','on', ...
    'SaveFormat','Dataset');

add_power_stage(mdl, secGan);
try
    set_param(mdl + "/Solver_Config", ...
        'UseLocalSolver','on', ...
        'LocalSolverChoice','NE_BACKWARD_EULER_ADVANCER', ...
        'LocalSolverSampleTime','5e-8', ...
        'MaxNonlinIter','2', ...
        'ConsistencyTolSource','LOCAL', ...
        'ConsistencyAbsTol','1e-4', ...
        'ConsistencyRelTol','1e-4');
catch
end
shift_dab_power_stage(mdl, 800);
if mpptMode
    shift_dab_power_stage(mdl, 350);
    set_param(mdl + "/Rload_DC_Output", 'R','24', 'R_unit','Ohm');
    add_mppt_measurements(mdl);
end
add_measurements(mdl, mpptMode);
add_gate_control(mdl, mpptMode);
save_system(mdl, dst);
end

function add_power_stage(mdl, secGan)
% Source, input EMI, and realistic passives
add_block('ee_lib/Sources/Voltage Source', mdl + "/VIN_18_32V_Bench_Source", 'Position',[40 315 100 385]);
set_param(mdl + "/VIN_18_32V_Bench_Source", 'dc_voltage','24', 'dc_voltage_unit','V');
add_block('ee_lib/Passive/Resistor', mdl + "/RSRC_Bench_Output_30mOhm", 'Position',[135 320 205 350]);
set_param(mdl + "/RSRC_Bench_Output_30mOhm", 'R','0.03', 'R_unit','Ohm');
add_block('ee_lib/Passive/Inductor', mdl + "/LSRC_Cable_200nH", 'Position',[235 318 315 353]);
set_param(mdl + "/LSRC_Cable_200nH", 'l','0.2', 'l_unit','uH', 'r','0.003', 'r_unit','Ohm', 'max_working_current','50', 'max_working_current_unit','A');
add_block('fl_lib/Electrical/Electrical Sensors/Current Sensor', mdl + "/Input_Current_Sensor", 'Position',[345 310 415 365]);
add_block('ee_lib/Passive/Resistor', mdl + "/RIN_Shunt_1mOhm", 'Position',[445 320 510 350]);
set_param(mdl + "/RIN_Shunt_1mOhm", 'R','0.001', 'R_unit','Ohm');
add_block('ee_lib/Passive/Inductor', mdl + "/LIN_Input_CM_DM_Filter", 'Position',[535 315 625 360]);
set_param(mdl + "/LIN_Input_CM_DM_Filter", 'l','1.0', 'l_unit','uH', 'r','0.004', 'r_unit','Ohm', 'max_working_current','50', 'max_working_current_unit','A');
add_block('ee_lib/Passive/Inductor', mdl + "/LRET_Input_CM_Filter", 'Position',[285 515 375 560]);
set_param(mdl + "/LRET_Input_CM_Filter", 'l','1.0', 'l_unit','uH', 'r','0.004', 'r_unit','Ohm', 'max_working_current','50', 'max_working_current_unit','A');
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_Input_Bulk", 'Position',[675 300 735 385]);
set_param(mdl + "/CIN_Input_Bulk", 'c','1000', 'c_unit','uF', 'r','0.003', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_Input_Bulk", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_HF_MLCC_Bank", 'Position',[775 300 835 385]);
set_param(mdl + "/CIN_HF_MLCC_Bank", 'c','220', 'c_unit','uF', 'r','0.0008', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_HF_MLCC_Bank", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_X2_Film_Cap", 'Position',[875 300 935 385]);
set_param(mdl + "/CIN_X2_Film_Cap", 'c','2.2', 'c_unit','uF', 'r','0.01', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_X2_Film_Cap", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', mdl + "/RIN_Bus_Bleeder", 'Position',[970 300 1020 385]);
set_param(mdl + "/RIN_Bus_Bleeder", 'R','100000', 'R_unit','Ohm');
set_param(mdl + "/RIN_Bus_Bleeder", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', mdl + "/RIN_Damping_Branch", 'Position',[620 500 685 545]);
set_param(mdl + "/RIN_Damping_Branch", 'R','0.33', 'R_unit','Ohm');
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_Damping_Cap", 'Position',[735 500 795 545]);
set_param(mdl + "/CIN_Damping_Cap", 'c','100', 'c_unit','uF', 'r','0.02', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_Damping_Cap", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_Y_Pos_to_PE", 'Position',[255 605 315 650]);
set_param(mdl + "/CIN_Y_Pos_to_PE", 'c','2.2', 'c_unit','nF', 'r','0.02', 'r_unit','Ohm');
set_param(mdl + "/CIN_Y_Pos_to_PE", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_Y_Neg_to_PE", 'Position',[380 605 440 650]);
set_param(mdl + "/CIN_Y_Neg_to_PE", 'c','2.2', 'c_unit','nF', 'r','0.02', 'r_unit','Ohm');
set_param(mdl + "/CIN_Y_Neg_to_PE", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', mdl + "/RPE_Primary_Bleed", 'Position',[500 605 560 650]);
set_param(mdl + "/RPE_Primary_Bleed", 'R','100000000', 'R_unit','Ohm');
add_block('fl_lib/Electrical/Electrical Elements/Electrical Reference', mdl + "/PRI_Return_Ref", 'Position',[675 445 725 495]);
add_block('nesl_utility/Solver Configuration', mdl + "/Solver_Config", 'Position',[45 455 100 510]);

add_real_transformer(mdl, "T1_HF_Transformer_Real", [835 205 1125 455]);

add_block('ee_lib/Passive/Capacitor', mdl + "/CSEC_DC_Link_Cap", 'Position',[1665 280 1730 385]);
set_param(mdl + "/CSEC_DC_Link_Cap", 'c','47', 'c_unit','uF', 'r','0.015', 'r_unit','Ohm', 'max_working_voltage_range','[-100,100]', 'max_working_voltage_range_unit','V', 'vc_specify','on', 'vc','48', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CSEC_DC_Link_Cap", 'Orientation','down');
add_block('ee_lib/Passive/Inductor', mdl + "/LOUT_Filter_Inductor", 'Position',[1785 310 1885 360]);
set_param(mdl + "/LOUT_Filter_Inductor", 'l','4.7', 'l_unit','uH', 'r','0.008', 'r_unit','Ohm', 'max_working_current','35', 'max_working_current_unit','A');
add_block('ee_lib/Passive/Capacitor', mdl + "/COUT_DC_Bulk", 'Position',[1950 280 2020 385]);
set_param(mdl + "/COUT_DC_Bulk", 'c','470', 'c_unit','uF', 'r','0.008', 'r_unit','Ohm', 'max_working_voltage_range','[-100,100]', 'max_working_voltage_range_unit','V', 'vc_specify','on', 'vc','48', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/COUT_DC_Bulk", 'Orientation','down');
add_block('fl_lib/Electrical/Electrical Sensors/Current Sensor', mdl + "/Output_Current_Sensor", 'Position',[2075 300 2145 355]);
add_block('ee_lib/Passive/Inductor', mdl + "/LOUT_EMI_Pos_Choke", 'Position',[2075 210 2165 255]);
set_param(mdl + "/LOUT_EMI_Pos_Choke", 'l','2.2', 'l_unit','uH', 'r','0.012', 'r_unit','Ohm', 'max_working_current','35', 'max_working_current_unit','A');
add_block('ee_lib/Passive/Inductor', mdl + "/LOUT_EMI_Return_Choke", 'Position',[2075 440 2165 485]);
set_param(mdl + "/LOUT_EMI_Return_Choke", 'l','2.2', 'l_unit','uH', 'r','0.012', 'r_unit','Ohm', 'max_working_current','35', 'max_working_current_unit','A');
add_block('ee_lib/Passive/Capacitor', mdl + "/COUT_X_Film_Cap", 'Position',[2195 405 2255 460]);
set_param(mdl + "/COUT_X_Film_Cap", 'c','2.2', 'c_unit','uF', 'r','0.01', 'r_unit','Ohm', 'vc_specify','on', 'vc','48', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/COUT_X_Film_Cap", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', mdl + "/ROUT_Bleeder", 'Position',[2290 405 2350 460]);
set_param(mdl + "/ROUT_Bleeder", 'R','100000', 'R_unit','Ohm');
set_param(mdl + "/ROUT_Bleeder", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', mdl + "/COUT_Y_Pos_to_PE", 'Position',[2195 525 2255 570]);
set_param(mdl + "/COUT_Y_Pos_to_PE", 'c','2.2', 'c_unit','nF', 'r','0.02', 'r_unit','Ohm');
set_param(mdl + "/COUT_Y_Pos_to_PE", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', mdl + "/COUT_Y_Neg_to_PE", 'Position',[2290 525 2350 570]);
set_param(mdl + "/COUT_Y_Neg_to_PE", 'c','2.2', 'c_unit','nF', 'r','0.02', 'r_unit','Ohm');
set_param(mdl + "/COUT_Y_Neg_to_PE", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', mdl + "/RPE_Output_Bleed", 'Position',[2380 525 2440 570]);
set_param(mdl + "/RPE_Output_Bleed", 'R','100000000', 'R_unit','Ohm');
add_block('ee_lib/Passive/Resistor', mdl + "/Rload_DC_Output", 'Position',[2205 285 2275 390]);
set_param(mdl + "/Rload_DC_Output", 'R','23.04', 'R_unit','Ohm');
set_param(mdl + "/Rload_DC_Output", 'Orientation','down');
add_block('fl_lib/Electrical/Electrical Elements/Electrical Reference', mdl + "/SEC_Return_Ref", 'Position',[1950 440 2000 490]);

% Full bridges contain the actual nonideal MOSFET/GaN devices and gate networks.
add_full_bridge(mdl, "PRI_Full_Bridge_RealDrivers", [355 115 735 560], false, ...
    ["Q1_PRI_A_HS","Q2_PRI_A_LS","Q3_PRI_B_HS","Q4_PRI_B_LS"]);
add_full_bridge(mdl, "SEC_Full_Bridge_RealDrivers", [1240 115 1620 560], secGan, ...
    ["Q5_SEC_A_HS","Q6_SEC_A_LS","Q7_SEC_B_HS","Q8_SEC_B_LS"]);

% Primary DC bus with source impedance, shunt, and EMI elements
connect(mdl, "VIN_18_32V_Bench_Source", "LConn", 1, "RSRC_Bench_Output_30mOhm", "LConn", 1);
connect(mdl, "RSRC_Bench_Output_30mOhm", "RConn", 1, "LSRC_Cable_200nH", "LConn", 1);
connect(mdl, "LSRC_Cable_200nH", "RConn", 1, "Input_Current_Sensor", "LConn", 1);
connect(mdl, "Input_Current_Sensor", "RConn", 2, "RIN_Shunt_1mOhm", "LConn", 1);
connect(mdl, "RIN_Shunt_1mOhm", "RConn", 1, "LIN_Input_CM_DM_Filter", "LConn", 1);
connect(mdl, "LIN_Input_CM_DM_Filter", "RConn", 1, "CIN_Input_Bulk", "LConn", 1);
connect(mdl, "VIN_18_32V_Bench_Source", "RConn", 1, "LRET_Input_CM_Filter", "LConn", 1);
connect(mdl, "LRET_Input_CM_Filter", "RConn", 1, "CIN_Input_Bulk", "RConn", 1);
connect(mdl, "CIN_Input_Bulk", "LConn", 1, "CIN_HF_MLCC_Bank", "LConn", 1);
connect(mdl, "CIN_HF_MLCC_Bank", "RConn", 1, "CIN_Input_Bulk", "RConn", 1);
connect(mdl, "CIN_Input_Bulk", "LConn", 1, "CIN_X2_Film_Cap", "LConn", 1);
connect(mdl, "CIN_X2_Film_Cap", "RConn", 1, "CIN_Input_Bulk", "RConn", 1);
connect(mdl, "CIN_Input_Bulk", "LConn", 1, "RIN_Bus_Bleeder", "LConn", 1);
connect(mdl, "RIN_Bus_Bleeder", "RConn", 1, "CIN_Input_Bulk", "RConn", 1);
connect(mdl, "CIN_Input_Bulk", "LConn", 1, "RIN_Damping_Branch", "LConn", 1);
connect(mdl, "RIN_Damping_Branch", "RConn", 1, "CIN_Damping_Cap", "LConn", 1);
connect(mdl, "CIN_Damping_Cap", "RConn", 1, "CIN_Input_Bulk", "RConn", 1);
connect(mdl, "CIN_Input_Bulk", "LConn", 1, "CIN_Y_Pos_to_PE", "LConn", 1);
connect(mdl, "CIN_Y_Pos_to_PE", "RConn", 1, "RPE_Primary_Bleed", "LConn", 1);
connect(mdl, "CIN_Input_Bulk", "RConn", 1, "CIN_Y_Neg_to_PE", "LConn", 1);
connect(mdl, "CIN_Y_Neg_to_PE", "RConn", 1, "RPE_Primary_Bleed", "LConn", 1);
connect(mdl, "RPE_Primary_Bleed", "RConn", 1, "PRI_Return_Ref", "LConn", 1);
connect(mdl, "CIN_Input_Bulk", "RConn", 1, "PRI_Return_Ref", "LConn", 1);
connect(mdl, "PRI_Return_Ref", "LConn", 1, "Solver_Config", "RConn", 1);

% Correct primary full bridge: P/N are DC bus, A/B are transformer AC nodes.
connect(mdl, "CIN_Input_Bulk", "LConn", 1, "PRI_Full_Bridge_RealDrivers", "LConn", 1);
connect(mdl, "CIN_Input_Bulk", "RConn", 1, "PRI_Full_Bridge_RealDrivers", "LConn", 2);

% Real transformer subsystem includes leakage, winding resistance, magnetizing inductance, and parasitics.
connect(mdl, "PRI_Full_Bridge_RealDrivers", "RConn", 1, "T1_HF_Transformer_Real", "LConn", 1);
connect(mdl, "PRI_Full_Bridge_RealDrivers", "RConn", 2, "T1_HF_Transformer_Real", "LConn", 2);

% Secondary bridge
connect(mdl, "CSEC_DC_Link_Cap", "LConn", 1, "SEC_Full_Bridge_RealDrivers", "LConn", 1);
connect(mdl, "CSEC_DC_Link_Cap", "RConn", 1, "SEC_Full_Bridge_RealDrivers", "LConn", 2);
connect(mdl, "T1_HF_Transformer_Real", "RConn", 1, "SEC_Full_Bridge_RealDrivers", "RConn", 1);
connect(mdl, "T1_HF_Transformer_Real", "RConn", 2, "SEC_Full_Bridge_RealDrivers", "RConn", 2);
connect(mdl, "CSEC_DC_Link_Cap", "RConn", 1, "COUT_DC_Bulk", "RConn", 1);
connect(mdl, "CSEC_DC_Link_Cap", "LConn", 1, "LOUT_Filter_Inductor", "LConn", 1);
connect(mdl, "LOUT_Filter_Inductor", "RConn", 1, "COUT_DC_Bulk", "LConn", 1);
connect(mdl, "COUT_DC_Bulk", "RConn", 1, "SEC_Return_Ref", "LConn", 1);
connect(mdl, "COUT_DC_Bulk", "LConn", 1, "LOUT_EMI_Pos_Choke", "LConn", 1);
connect(mdl, "LOUT_EMI_Pos_Choke", "RConn", 1, "Output_Current_Sensor", "LConn", 1);
connect(mdl, "Output_Current_Sensor", "RConn", 2, "Rload_DC_Output", "LConn", 1);
connect(mdl, "Rload_DC_Output", "RConn", 1, "LOUT_EMI_Return_Choke", "LConn", 1);
connect(mdl, "LOUT_EMI_Return_Choke", "RConn", 1, "COUT_DC_Bulk", "RConn", 1);
connect(mdl, "Rload_DC_Output", "LConn", 1, "COUT_X_Film_Cap", "LConn", 1);
connect(mdl, "COUT_X_Film_Cap", "RConn", 1, "Rload_DC_Output", "RConn", 1);
connect(mdl, "Rload_DC_Output", "LConn", 1, "ROUT_Bleeder", "LConn", 1);
connect(mdl, "ROUT_Bleeder", "RConn", 1, "Rload_DC_Output", "RConn", 1);
connect(mdl, "Rload_DC_Output", "LConn", 1, "COUT_Y_Pos_to_PE", "LConn", 1);
connect(mdl, "COUT_Y_Pos_to_PE", "RConn", 1, "RPE_Output_Bleed", "LConn", 1);
connect(mdl, "Rload_DC_Output", "RConn", 1, "COUT_Y_Neg_to_PE", "LConn", 1);
connect(mdl, "COUT_Y_Neg_to_PE", "RConn", 1, "RPE_Output_Bleed", "LConn", 1);
connect(mdl, "RPE_Output_Bleed", "RConn", 1, "SEC_Return_Ref", "LConn", 1);
end

function add_full_bridge(mdl, bridgeName, pos, isGan, fetNames)
bridgePath = mdl + "/" + bridgeName;
add_block('simulink/Ports & Subsystems/Subsystem', bridgePath, 'Position',pos);
try, delete_line(bridgePath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(bridgePath + "/Out1"); catch, end
set_param(bridgePath, 'Mask','on', 'MaskDisplay',"color('black');disp('" + strrep(bridgeName,'_',' ') + "\nnonideal FETs + gate RCD');");
try, set_param(bridgePath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(bridgePath, 'MaskIconOpaque','opaque'); catch, end
set_param(bridgePath + "/In1", 'Name','G1_A_HS', 'Position',[35 145 65 165]);
add_block('simulink/Sources/In1', bridgePath + "/G2_A_LS", 'Position',[35 195 65 215]);
add_block('simulink/Sources/In1', bridgePath + "/G3_B_HS", 'Position',[35 455 65 475]);
add_block('simulink/Sources/In1', bridgePath + "/G4_B_LS", 'Position',[35 505 65 525]);
add_block('nesl_utility/Connection Port', bridgePath + "/P_BUS", 'Position',[35 70 65 100]);
add_block('nesl_utility/Connection Port', bridgePath + "/N_BUS", 'Position',[35 650 65 680]);
add_block('nesl_utility/Connection Port', bridgePath + "/SW_A", 'Position',[780 235 810 265]);
add_block('nesl_utility/Connection Port', bridgePath + "/SW_B", 'Position',[780 545 810 575]);
set_param(bridgePath + "/P_BUS", 'Side','Left');
set_param(bridgePath + "/N_BUS", 'Side','Left');
set_param(bridgePath + "/SW_A", 'Side','Right');
set_param(bridgePath + "/SW_B", 'Side','Right');

add_half_bridge(bridgePath, "Leg_A_RealSwitchingCell", [185 110 675 330], isGan, fetNames(1), fetNames(2));
add_half_bridge(bridgePath, "Leg_B_RealSwitchingCell", [185 420 675 640], isGan, fetNames(3), fetNames(4));

add_line(bridgePath, 'G1_A_HS/1', 'Leg_A_RealSwitchingCell/1', 'autorouting','on');
add_line(bridgePath, 'G2_A_LS/1', 'Leg_A_RealSwitchingCell/2', 'autorouting','on');
add_line(bridgePath, 'G3_B_HS/1', 'Leg_B_RealSwitchingCell/1', 'autorouting','on');
add_line(bridgePath, 'G4_B_LS/1', 'Leg_B_RealSwitchingCell/2', 'autorouting','on');
connect(bridgePath, "P_BUS", "RConn", 1, "Leg_A_RealSwitchingCell", "LConn", 1);
connect(bridgePath, "N_BUS", "RConn", 1, "Leg_A_RealSwitchingCell", "LConn", 2);
connect(bridgePath, "P_BUS", "RConn", 1, "Leg_B_RealSwitchingCell", "LConn", 1);
connect(bridgePath, "N_BUS", "RConn", 1, "Leg_B_RealSwitchingCell", "LConn", 2);
connect(bridgePath, "Leg_A_RealSwitchingCell", "RConn", 1, "SW_A", "RConn", 1);
connect(bridgePath, "Leg_B_RealSwitchingCell", "RConn", 1, "SW_B", "RConn", 1);
end

function add_half_bridge(parent, legName, pos, isGan, highFetName, lowFetName)
legPath = parent + "/" + legName;
add_block('simulink/Ports & Subsystems/Subsystem', legPath, 'Position',pos);
try, delete_line(legPath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(legPath + "/Out1"); catch, end
set_param(legPath, 'Mask','on', 'MaskDisplay',"color('black');disp('" + strrep(legName,'_',' ') + "\nHS/LS nonideal FETs');");
try, set_param(legPath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(legPath, 'MaskIconOpaque','opaque'); catch, end
set_param(legPath + "/In1", 'Name','G_HS', 'Position',[25 125 55 145]);
add_block('simulink/Sources/In1', legPath + "/G_LS", 'Position',[25 335 55 355]);
add_block('nesl_utility/Connection Port', legPath + "/P_BUS", 'Position',[25 65 55 95]);
add_block('nesl_utility/Connection Port', legPath + "/N_BUS", 'Position',[25 455 55 485]);
add_block('nesl_utility/Connection Port', legPath + "/SW_NODE", 'Position',[825 255 855 285]);
set_param(legPath + "/P_BUS", 'Side','Left');
set_param(legPath + "/N_BUS", 'Side','Left');
set_param(legPath + "/SW_NODE", 'Side','Right');

hsPos = [390 105 480 195];
lsPos = [390 315 480 405];
add_block('ee_lib/Semiconductors & Converters/N-Channel MOSFET', legPath + "/" + highFetName, 'Position',hsPos);
configure_power_fet(legPath + "/" + highFetName, isGan);
add_block('ee_lib/Semiconductors & Converters/N-Channel MOSFET', legPath + "/" + lowFetName, 'Position',lsPos);
configure_power_fet(legPath + "/" + lowFetName, isGan);
add_gate_driver(legPath, highFetName, hsPos, isGan);
add_gate_driver(legPath, lowFetName, lsPos, isGan);

add_line(legPath, 'G_HS/1', extractBefore(highFetName, "_") + "_DRV_EQ/1", 'autorouting','on');
add_line(legPath, 'G_LS/1', extractBefore(lowFetName, "_") + "_DRV_EQ/1", 'autorouting','on');
connect(legPath, "P_BUS", "RConn", 1, highFetName, "RConn", 1);
connect(legPath, highFetName, "RConn", 2, lowFetName, "RConn", 1);
connect(legPath, highFetName, "RConn", 2, "SW_NODE", "RConn", 1);
connect(legPath, lowFetName, "RConn", 2, "N_BUS", "RConn", 1);
end

function configure_power_fet(blockPath, isGan)
if isGan
    set_param(blockPath, 'Rds','0.0017', 'Rds_unit','Ohm', 'RS','0.00005', 'RS_unit','Ohm', 'RD','0.00005', 'RD_unit','Ohm', 'Vth','1.7', 'Vth_unit','V', 'Rds_T2','0.0023', 'Rds_T2_unit','Ohm', ...
        'C_iss','1', 'C_iss_unit','pF', 'C_rss','0', 'C_rss_unit','pF', 'C_GS','1', 'C_GS_unit','pF', 'C_GD','0', 'C_GD_unit','pF', 'C_oss','650', 'C_oss_unit','pF', 'C_DS','650', 'C_DS_unit','pF', 'Cj0Diode','450', 'Cj0Diode_unit','pF');
else
    set_param(blockPath, 'Rds','0.0025', 'Rds_unit','Ohm', 'RS','0.0001', 'RS_unit','Ohm', 'RD','0.0001', 'RD_unit','Ohm', 'Vth','3.0', 'Vth_unit','V', 'Rds_T2','0.0045', 'Rds_T2_unit','Ohm', ...
        'C_iss','1', 'C_iss_unit','pF', 'C_rss','0', 'C_rss_unit','pF', 'C_GS','1', 'C_GS_unit','pF', 'C_GD','0', 'C_GD_unit','pF', 'C_oss','4200', 'C_oss_unit','pF', 'C_DS','4200', 'C_DS_unit','pF', 'Cj0Diode','1800', 'Cj0Diode_unit','pF');
end
end

function add_real_transformer(mdl, blockName, pos)
txPath = mdl + "/" + blockName;
add_block('simulink/Ports & Subsystems/Subsystem', txPath, 'Position',pos);
try, delete_line(txPath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(txPath + "/In1"); catch, end
try, delete_block(txPath + "/Out1"); catch, end
set_param(txPath, 'Mask','on', 'MaskDisplay',"color('black');disp('T1 PQ4040 PC95\nNp:Ns 1:2, Llk_sec 0.5uH\nLm_sec 230uH, DCR 0.95/3mR');");
try, set_param(txPath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(txPath, 'MaskIconOpaque','opaque'); catch, end
try
    ann = Simulink.Annotation(char(txPath), sprintf(['PQ4040 / PC95, Np:Ns = 1:2\n' ...
        'Lm = 230 uH on secondary, 57.5 uH primary equivalent\n' ...
        'Leakage = 0.5 uH on secondary, primary shorted during test\n' ...
        'DCR max: primary 0.95 mOhm, secondary 3.0 mOhm']));
    ann.Position = [115 450 650 535];
    ann.FontSize = 10;
catch
end

add_block('nesl_utility/Connection Port', txPath + "/PRI_SW_A", 'Position',[25 115 55 145]);
add_block('nesl_utility/Connection Port', txPath + "/PRI_SW_B", 'Position',[25 315 55 345]);
add_block('nesl_utility/Connection Port', txPath + "/SEC_SW_A", 'Position',[760 115 790 145]);
add_block('nesl_utility/Connection Port', txPath + "/SEC_SW_B", 'Position',[760 315 790 345]);
set_param(txPath + "/PRI_SW_A", 'Side','Left');
set_param(txPath + "/PRI_SW_B", 'Side','Left');
set_param(txPath + "/SEC_SW_A", 'Side','Right');
set_param(txPath + "/SEC_SW_B", 'Side','Right');

add_block('ee_lib/Passive/Resistor', txPath + "/Rpri_Winding", 'Position',[105 122 170 152]);
set_param(txPath + "/Rpri_Winding", 'R','0.00095', 'R_unit','Ohm');
add_block('fl_lib/Electrical/Electrical Elements/Mutual Inductor', txPath + "/Coupled_Core", 'Position',[260 75 370 245]);
set_param(txPath + "/Coupled_Core", 'L1','57.5', 'L1_unit','uH', 'L2','230', 'L2_unit','uH', 'k','0.999999');
add_block('ee_lib/Passive/Resistor', txPath + "/Rsec_Winding", 'Position',[455 122 520 152]);
set_param(txPath + "/Rsec_Winding", 'R','0.0030', 'R_unit','Ohm');
add_block('ee_lib/Passive/Inductor', txPath + "/Llk_Secondary_Transfer", 'Position',[560 120 645 155]);
set_param(txPath + "/Llk_Secondary_Transfer", 'l','0.5', 'l_unit','uH', 'r','0.0001', 'r_unit','Ohm', 'max_working_current','50', 'max_working_current_unit','A');

add_block('ee_lib/Passive/Capacitor', txPath + "/Cpri_Winding", 'Position',[375 275 425 330]);
set_param(txPath + "/Cpri_Winding", 'c','220', 'c_unit','pF', 'r','0.05', 'r_unit','Ohm');
set_param(txPath + "/Cpri_Winding", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', txPath + "/Csec_Winding", 'Position',[535 275 585 330]);
set_param(txPath + "/Csec_Winding", 'c','150', 'c_unit','pF', 'r','0.05', 'r_unit','Ohm');
set_param(txPath + "/Csec_Winding", 'Orientation','down');
add_block('ee_lib/Passive/Capacitor', txPath + "/Cps_Interwinding", 'Position',[455 365 515 410]);
set_param(txPath + "/Cps_Interwinding", 'c','100', 'c_unit','pF', 'r','0.1', 'r_unit','Ohm');

connect(txPath, "PRI_SW_A", "RConn", 1, "Rpri_Winding", "LConn", 1);
connect(txPath, "Rpri_Winding", "RConn", 1, "Coupled_Core", "LConn", 1);
connect(txPath, "PRI_SW_B", "RConn", 1, "Coupled_Core", "LConn", 2);
connect(txPath, "Coupled_Core", "RConn", 1, "Rsec_Winding", "LConn", 1);
connect(txPath, "Rsec_Winding", "RConn", 1, "Llk_Secondary_Transfer", "LConn", 1);
connect(txPath, "Llk_Secondary_Transfer", "RConn", 1, "SEC_SW_A", "RConn", 1);
connect(txPath, "Coupled_Core", "RConn", 2, "SEC_SW_B", "RConn", 1);

connect(txPath, "Coupled_Core", "LConn", 1, "Cpri_Winding", "LConn", 1);
connect(txPath, "Cpri_Winding", "RConn", 1, "Coupled_Core", "LConn", 2);
connect(txPath, "Llk_Secondary_Transfer", "RConn", 1, "Csec_Winding", "LConn", 1);
connect(txPath, "Csec_Winding", "RConn", 1, "Coupled_Core", "RConn", 2);
connect(txPath, "Coupled_Core", "LConn", 1, "Cps_Interwinding", "LConn", 1);
connect(txPath, "Cps_Interwinding", "RConn", 1, "Coupled_Core", "RConn", 1);
end

function shift_dab_power_stage(mdl, dx)
names = [
    "PRI_Full_Bridge_RealDrivers","SEC_Full_Bridge_RealDrivers", ...
    "Q1_PRI_A_HS","Q2_PRI_A_LS","Q3_PRI_B_HS","Q4_PRI_B_LS", ...
    "Q5_SEC_A_HS","Q6_SEC_A_LS","Q7_SEC_B_HS","Q8_SEC_B_LS", ...
    "Q1_GateDriver_10V","Q2_GateDriver_10V","Q3_GateDriver_10V","Q4_GateDriver_10V", ...
    "Q5_GateDriver_10V","Q6_GateDriver_10V","Q7_GateDriver_10V","Q8_GateDriver_10V", ...
    "Q1_Gate_PS","Q2_Gate_PS","Q3_Gate_PS","Q4_Gate_PS", ...
    "Q5_Gate_PS","Q6_Gate_PS","Q7_Gate_PS","Q8_Gate_PS", ...
    "T1_HF_Transformer_Real","CSEC_DC_Link_Cap", ...
    "LOUT_Filter_Inductor","COUT_DC_Bulk","LOUT_EMI_Pos_Choke","LOUT_EMI_Return_Choke", ...
    "COUT_X_Film_Cap","ROUT_Bleeder","COUT_Y_Pos_to_PE","COUT_Y_Neg_to_PE", ...
    "RPE_Output_Bleed","Output_Current_Sensor","Rload_DC_Output","SEC_Return_Ref" ...
];
for q = 1:8
    prefix = "Q" + q;
    names = [names, prefix + "_DRV_EQ"];
end
for i = 1:numel(names)
    blk = mdl + "/" + names(i);
    if ishandle(getSimulinkBlockHandle(blk))
        pos = get_param(blk, 'Position');
        set_param(blk, 'Position', pos + [dx 0 dx 0]);
    end
end
end

function add_gate_driver(mdl, fetName, fetPos, isGan)
prefix = extractBefore(fetName, "_");
if isGan
    gateHigh = 6.0; gateLow = -1.5; rgOnValue = "5.1"; rgOffValue = "1.5"; snubCValue = "680";
else
    gateHigh = 10.0; gateLow = -2.0; rgOnValue = "4.7"; rgOffValue = "1.2"; snubCValue = "1000";
end
gateGain = (gateHigh - gateLow) / 10.0;

cellPos = [fetPos(1)-230 fetPos(2) fetPos(1)-45 fetPos(4)];
cellName = prefix + "_DRV_EQ";
cellPath = mdl + "/" + cellName;
add_block('simulink/Ports & Subsystems/Subsystem', cellPath, 'Position',cellPos);
set_param(cellPath, 'ShowName','on', 'Mask','on', 'MaskDisplay',"color('black');disp('" + prefix + " driver\nRg/RCD + D-S snubber');");
try, set_param(cellPath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(cellPath, 'MaskIconOpaque','opaque'); catch, end
try, delete_line(cellPath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(cellPath + "/Out1"); catch, end
set_param(cellPath + "/In1", 'Name','GateCmd_0_10', 'Position',[30 115 60 135]);

add_block('nesl_utility/Connection Port', cellPath + "/D", 'Position',[835 65 865 95]);
add_block('nesl_utility/Connection Port', cellPath + "/G", 'Position',[835 190 865 220]);
add_block('nesl_utility/Connection Port', cellPath + "/S", 'Position',[835 365 865 395]);
set_param(cellPath + "/D", 'Side','Right');
set_param(cellPath + "/G", 'Side','Right');
set_param(cellPath + "/S", 'Side','Right');
add_block('simulink/Math Operations/Gain', cellPath + "/GateScale", 'Position',[115 107 165 143], 'Gain',num2str(gateGain,'%.4g'));
add_block('simulink/Math Operations/Bias', cellPath + "/GateBias", 'Position',[205 107 255 143], 'Bias',num2str(gateLow,'%.4g'));
add_block('nesl_utility/Simulink-PS Converter', cellPath + "/Gate_PS", 'Position',[300 108 355 142]);
set_param(cellPath + "/Gate_PS", 'Unit','V');
add_block('fl_lib/Electrical/Electrical Sources/Controlled Voltage Source', cellPath + "/Driver_Vgs_Source", 'Position',[415 95 485 175]);
add_block('ee_lib/Passive/Resistor', cellPath + "/Rg_On", 'Position',[570 135 640 165]);
set_param(cellPath + "/Rg_On", 'R',rgOnValue, 'R_unit','Ohm');
add_block('ee_lib/Passive/Resistor', cellPath + "/Rg_Off", 'Position',[650 255 720 285]);
set_param(cellPath + "/Rg_Off", 'R',rgOffValue, 'R_unit','Ohm');
add_block('ee_lib/Semiconductors & Converters/Diode', cellPath + "/FastOff_Diode", 'Position',[540 255 590 285]);
add_block('ee_lib/Passive/Resistor', cellPath + "/Gate_Pulldown", 'Position',[740 250 790 330]);
set_param(cellPath + "/Gate_Pulldown", 'R','100000', 'R_unit','Ohm');
set_param(cellPath + "/Gate_Pulldown", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', cellPath + "/DS_Snubber_R", 'Position',[480 410 555 440]);
set_param(cellPath + "/DS_Snubber_R", 'R','6.8', 'R_unit','Ohm');
add_block('ee_lib/Passive/Capacitor', cellPath + "/DS_Snubber_C", 'Position',[610 400 665 455]);
set_param(cellPath + "/DS_Snubber_C", 'c',snubCValue, 'c_unit','pF', 'r','0.05', 'r_unit','Ohm');
set_param(cellPath + "/DS_Snubber_C", 'Orientation','down');

add_line(cellPath, 'GateCmd_0_10/1', 'GateScale/1', 'autorouting','on');
add_line(cellPath, 'GateScale/1', 'GateBias/1', 'autorouting','on');
add_line(cellPath, 'GateBias/1', 'Gate_PS/1', 'autorouting','on');
connect(cellPath, "Gate_PS", "RConn", 1, "Driver_Vgs_Source", "RConn", 1);
connect(cellPath, "Driver_Vgs_Source", "LConn", 1, "Rg_On", "LConn", 1);
connect(cellPath, "Rg_On", "RConn", 1, "G", "RConn", 1);
connect(cellPath, "G", "RConn", 1, "Rg_Off", "LConn", 1);
connect(cellPath, "Rg_Off", "RConn", 1, "FastOff_Diode", "LConn", 1);
connect(cellPath, "FastOff_Diode", "RConn", 1, "Driver_Vgs_Source", "LConn", 1);
connect(cellPath, "G", "RConn", 1, "Gate_Pulldown", "LConn", 1);
connect(cellPath, "Gate_Pulldown", "RConn", 1, "S", "RConn", 1);
connect(cellPath, "Driver_Vgs_Source", "RConn", 2, "S", "RConn", 1);
connect(cellPath, "D", "RConn", 1, "DS_Snubber_R", "LConn", 1);
connect(cellPath, "DS_Snubber_R", "RConn", 1, "DS_Snubber_C", "LConn", 1);
connect(cellPath, "DS_Snubber_C", "RConn", 1, "S", "RConn", 1);

connect(mdl, cellName, "RConn", 1, fetName, "RConn", 1);
connect(mdl, cellName, "RConn", 2, fetName, "LConn", 1);
connect(mdl, cellName, "RConn", 3, fetName, "RConn", 2);
end

function add_gate_control(mdl, mpptMode)
add_block('simulink/Sources/Clock', mdl + "/Sim_Time", 'Position',[220 900 250 930]);
add_block('simulink/Sources/Constant', mdl + "/MCU_Vout_Setpoint_V", 'Position',[205 800 295 830], 'Value','48');
add_block('simulink/Discrete/Unit Delay', mdl + "/ADC_OneCycle_Delay", 'Position',[245 955 295 985]);
set_param(mdl + "/ADC_OneCycle_Delay", 'SampleTime','1e-6');
if mpptMode
    chartName = "RA8P1_EdgeAI_MPPT_Phase";
    chartPath = mdl + "/" + chartName;
    add_block('simulink/Discrete/Unit Delay', mdl + "/ADC_Delay_Vpv", 'Position',[245 1005 295 1035]);
    add_block('simulink/Discrete/Unit Delay', mdl + "/ADC_Delay_Ipv", 'Position',[245 1055 295 1085]);
    set_param(mdl + "/ADC_Delay_Vpv", 'SampleTime','1e-6');
    set_param(mdl + "/ADC_Delay_Ipv", 'SampleTime','1e-6');
    add_block('simulink/User-Defined Functions/MATLAB Function', chartPath, 'Position',[350 820 640 1045]);
else
    chartName = "RA8P1_PhaseShift_PI";
    chartPath = mdl + "/" + chartName;
    add_block('simulink/User-Defined Functions/MATLAB Function', chartPath, 'Position',[350 830 610 995]);
end
rt = sfroot;
chart = rt.find('-isa','Stateflow.EMChart','Path',chartPath);
if mpptMode
    chart.Script = mppt_gate_script();
else
    chart.Script = gate_script();
end
chart.ChartUpdate = 'DISCRETE';
chart.SampleTime = '1e-7';
for i = 1:8
    add_block('simulink/Signal Routing/Goto', mdl + "/Q" + i + "_GATE_Goto", 'Position',[665 790+35*i 755 815+35*i], 'GotoTag',"Q" + i + "_GATE", 'TagVisibility','local');
    set_param(mdl + "/Q" + i + "_GATE_Goto", 'ShowName','off');
    if i <= 4
        bridgeCell = "PRI_Full_Bridge_RealDrivers";
        bridgePort = i;
    else
        bridgeCell = "SEC_Full_Bridge_RealDrivers";
        bridgePort = i - 4;
    end
    bridgePos = get_param(mdl + "/" + bridgeCell, 'Position');
    gateFromPos = [bridgePos(1)-90, bridgePos(2)+30+35*bridgePort, bridgePos(1)-30, bridgePos(2)+55+35*bridgePort];
    add_block('simulink/Signal Routing/From', mdl + "/Q" + i + "_GATE_From", 'Position',gateFromPos, 'GotoTag',"Q" + i + "_GATE");
    set_param(mdl + "/Q" + i + "_GATE_From", 'ShowName','off');
    add_line(mdl, chartName + "/" + i, "Q" + i + "_GATE_Goto/1", 'autorouting','on');
    add_line(mdl, "Q" + i + "_GATE_From/1", bridgeCell + "/" + bridgePort, 'autorouting','on');
end
add_line(mdl, 'Sim_Time/1', chartName + "/1", 'autorouting','on');
add_block('simulink/Signal Routing/From', mdl + "/VOUT_MEAS_Control_From", 'Position',[245 955 325 980], 'GotoTag','VOUT_MEAS');
add_line(mdl, 'VOUT_MEAS_Control_From/1', 'ADC_OneCycle_Delay/1', 'autorouting','on');
add_line(mdl, 'ADC_OneCycle_Delay/1', chartName + "/2", 'autorouting','on');
if mpptMode
    add_line(mdl, 'Vpv_To_Simulink/1', 'ADC_Delay_Vpv/1', 'autorouting','on');
    add_line(mdl, 'Ipv_To_Simulink/1', 'ADC_Delay_Ipv/1', 'autorouting','on');
    add_line(mdl, 'ADC_Delay_Vpv/1', chartName + "/3", 'autorouting','on');
    add_line(mdl, 'ADC_Delay_Ipv/1', chartName + "/4", 'autorouting','on');
    add_line(mdl, 'MCU_Vout_Setpoint_V/1', chartName + "/5", 'autorouting','on');
    add_block('simulink/Sinks/To Workspace', mdl + "/MPPT_Vpv_Target_sl", 'Position',[700 1075 875 1105], 'VariableName', mdl + "_vpv_target", 'SaveFormat','StructureWithTime');
    add_block('simulink/Sinks/To Workspace', mdl + "/MPPT_Phase_us_sl", 'Position',[700 1125 875 1155], 'VariableName', mdl + "_phase_us", 'SaveFormat','StructureWithTime');
    add_block('simulink/Signal Routing/Mux', mdl + "/MPPT_Target_Phase_Mux", 'Position',[700 970 725 1030], 'Inputs','2');
    add_block('simulink/Sinks/Scope', mdl + "/Scope_MPPT_Target_Phase", 'Position',[770 960 930 1040]);
    add_line(mdl, chartName + "/9", 'MPPT_Vpv_Target_sl/1', 'autorouting','on');
    add_line(mdl, chartName + "/10", 'MPPT_Phase_us_sl/1', 'autorouting','on');
    add_line(mdl, chartName + "/9", 'MPPT_Target_Phase_Mux/1', 'autorouting','on');
    add_line(mdl, chartName + "/10", 'MPPT_Target_Phase_Mux/2', 'autorouting','on');
    add_line(mdl, 'MPPT_Target_Phase_Mux/1', 'Scope_MPPT_Target_Phase/1', 'autorouting','on');
else
    add_block('simulink/Signal Routing/From', mdl + "/VIN_MEAS_Control_From", 'Position',[245 1005 325 1030], 'GotoTag','VIN_MEAS');
    add_block('simulink/Signal Routing/From', mdl + "/IOUT_MEAS_Control_From", 'Position',[245 1055 325 1080], 'GotoTag','IOUT_MEAS');
    add_block('simulink/Discrete/Unit Delay', mdl + "/ADC_Delay_Vin", 'Position',[355 1002 405 1032]);
    add_block('simulink/Discrete/Unit Delay', mdl + "/ADC_Delay_Iout", 'Position',[355 1052 405 1082]);
    set_param(mdl + "/ADC_Delay_Vin", 'SampleTime','1e-6');
    set_param(mdl + "/ADC_Delay_Vin", 'InitialCondition','24');
    set_param(mdl + "/ADC_Delay_Iout", 'SampleTime','1e-6');
    add_block('simulink/Sinks/To Workspace', mdl + "/Phase_us_scope_sl", 'Position',[700 1045 875 1075], 'VariableName', mdl + "_phase_us", 'SaveFormat','StructureWithTime');
    add_line(mdl, 'MCU_Vout_Setpoint_V/1', chartName + "/3", 'autorouting','on');
    add_line(mdl, 'VIN_MEAS_Control_From/1', 'ADC_Delay_Vin/1', 'autorouting','on');
    add_line(mdl, 'ADC_Delay_Vin/1', chartName + "/4", 'autorouting','on');
    add_line(mdl, 'IOUT_MEAS_Control_From/1', 'ADC_Delay_Iout/1', 'autorouting','on');
    add_line(mdl, 'ADC_Delay_Iout/1', chartName + "/5", 'autorouting','on');
    add_line(mdl, chartName + "/9", 'Phase_us_scope_sl/1', 'autorouting','on');
end
end

function s = gate_script()
lines = [
"function [g1,g2,g3,g4,g5,g6,g7,g8,PhaseUs] = f(t,Vout,VrefCmd,Vin,Iout)"
"%#codegen"
"persistent phaseSec integ lastTick vFilt vinFilt iFilt"
"if isempty(phaseSec)"
"    phaseSec = 0.08e-6;"
"    integ = 0;"
"    lastTick = -1;"
"    vFilt = 0;"
"    vinFilt = 18;"
"    iFilt = 0;"
"end"
"fs = 300e3;"
"T = 1/fs;"
"dead = 120e-9;"
"TsCtrl = 20e-6;"
"tick = floor(t/TsCtrl);"
"Vref = min(max(VrefCmd,36),48);"
"if tick ~= lastTick"
"    if lastTick < 0"
"        vFilt = Vout;"
"        vinFilt = max(Vin,1);"
"        iFilt = max(Iout,0);"
"    else"
"        vFilt = vFilt + 0.08*(Vout - vFilt);"
"        vinFilt = vinFilt + 0.08*(max(Vin,1) - vinFilt);"
"        iFilt = iFilt + 0.25*(max(Iout,0) - iFilt);"
"    end"
"    err = Vref - vFilt;"
"    integ = min(max(integ + err*TsCtrl, -0.03), 0.03);"
"    vinFF = 24 / min(max(vinFilt,18),32);"
"    iBase = 100 / Vref;"
"    iDelta = iFilt - iBase;"
"    if iDelta < 0"
"        loadFF = 0.020e-6*iDelta;"
"    elseif iDelta <= 4"
"        loadFF = 0.065e-6*iDelta;"
"    else"
"        loadFF = min(0.340e-6, 0.260e-6 + 0.015e-6*(iDelta - 4));"
"    end"
"    if loadFF < 0"
"        loadScale = 1;"
"    elseif iDelta > 4"
"        loadScale = 1;"
"    else"
"        loadScale = min(max((Vref - 44)/4, 0), 1);"
"    end"
"    loadFF = loadScale * loadFF;"
"    phaseBase = phase_lookup(Vref) * vinFF + loadFF;"
"    phaseSec = phaseBase + 0.001e-6*err + 0.05e-6*integ;"
"    phaseSec = min(max(phaseSec, -0.36e-6), 0.55e-6);"
"    lastTick = tick;"
"end"
"phase = phaseSec;"
"[a,an] = halfbridge(t,T,dead);"
"[b,bn] = halfbridge(t+T/2,T,dead);"
"[c,cn] = halfbridge(t-phase,T,dead);"
"[d,dn] = halfbridge(t-phase+T/2,T,dead);"
"g1 = 10*a;  g2 = 10*an;"
"g3 = 10*b;  g4 = 10*bn;"
"g5 = 10*c;  g6 = 10*cn;"
"g7 = 10*d;  g8 = 10*dn;"
"PhaseUs = phaseSec*1e6;"
"end"
"function [hi,lo] = halfbridge(t,T,dead)"
"x = mod(t,T);"
"hi = double(x >= dead && x < T/2 - dead);"
"lo = double(x >= T/2 + dead && x < T - dead);"
"end"
"function p = phase_lookup(v)"
"if v <= 36"
"    p = -0.288e-6;"
"elseif v <= 40"
"    p = -0.288e-6 + (v-36)*(0.003e-6/4);"
"elseif v <= 44"
"    p = -0.285e-6 + (v-40)*(0.215e-6/4);"
"else"
"    p = -0.070e-6 + (v-44)*(0.145e-6/4);"
"end"
"end"
];
s = strjoin(lines, newline);
end

function s = mppt_gate_script()
lines = [
"function [g1,g2,g3,g4,g5,g6,g7,g8,VpvTarget,PhaseUs] = f(t,Vout,Vpv,Ipv,VrefCmd)"
"%#codegen"
"persistent phaseSec integ lastTick vFilt iFilt pFilt lastP lastV localTrim outFilt"
"if isempty(phaseSec)"
"    phaseSec = 0.10e-6;"
"    integ = 0;"
"    lastTick = -1;"
"    vFilt = 0;"
"    iFilt = 0;"
"    pFilt = 0;"
"    lastP = 0;"
"    lastV = 0;"
"    localTrim = 0;"
"    outFilt = 36;"
"end"
"fs = 300e3;"
"T = 1/fs;"
"dead = 120e-9;"
"TsCtrl = 20e-6;"
"tick = floor(t/TsCtrl);"
"VoutRef = min(max(VrefCmd,36),48);"
"if tick ~= lastTick"
"    if lastTick < 0"
"        vFilt = max(Vpv,0);"
"        iFilt = abs(Ipv);"
"        outFilt = Vout;"
"    else"
"        vFilt = vFilt + 0.08*(max(Vpv,0) - vFilt);"
"        iFilt = iFilt + 0.08*(abs(Ipv) - iFilt);"
"        outFilt = outFilt + 0.08*(Vout - outFilt);"
"    end"
"    pNow = vFilt*iFilt;"
"    pFilt = pFilt + 0.10*(pNow - pFilt);"
"    aiVmpp = 17.8;"
"    dP = pFilt - lastP;"
"    dV = vFilt - lastV;"
"    if abs(dP) > 0.08 && abs(dV) > 0.02"
"        localTrim = min(max(localTrim + 0.04*sign(dP)*sign(dV), -1.2), 1.2);"
"    end"
"    VpvTarget = min(max(aiVmpp + localTrim, 16.5), 18.1);"
"    err = VpvTarget - vFilt;"
"    integ = min(max(integ + err*TsCtrl, -0.02), 0.02);"
"    outErr = min(max(VoutRef - outFilt, -6), 6);"
"    phaseSec = 0.060e-6 + 1.5e-9*err + 1.0e-6*integ - 2.0e-9*outErr;"
"    if vFilt < 16.2"
"        phaseSec = max(phaseSec, 0.12e-6);"
"    end"
"    if outFilt > VoutRef + 0.8"
"        phaseSec = max(phaseSec, 0.16e-6);"
"    elseif Vout < 8 && t < 0.001"
"        phaseSec = min(phaseSec, 0.06e-6);"
"    end"
"    phaseSec = min(max(phaseSec, 0.005e-6), 0.35e-6);"
"    lastP = pFilt;"
"    lastV = vFilt;"
"    lastTick = tick;"
"else"
"    VpvTarget = 17.8;"
"end"
"phase = phaseSec;"
"PhaseUs = phaseSec*1e6;"
"[a,an] = halfbridge(t,T,dead);"
"[b,bn] = halfbridge(t+T/2,T,dead);"
"[c,cn] = halfbridge(t-phase,T,dead);"
"[d,dn] = halfbridge(t-phase+T/2,T,dead);"
"g1 = 10*a;  g2 = 10*an;"
"g3 = 10*b;  g4 = 10*bn;"
"g5 = 10*c;  g6 = 10*cn;"
"g7 = 10*d;  g8 = 10*dn;"
"end"
"function [hi,lo] = halfbridge(t,T,dead)"
"x = mod(t,T);"
"hi = double(x >= dead && x < T/2 - dead);"
"lo = double(x >= T/2 + dead && x < T - dead);"
"end"
];
s = strjoin(lines, newline);
end

function add_mppt_measurements(mdl)
add_block('fl_lib/Electrical/Electrical Sensors/Voltage Sensor', mdl + "/Vpv_Sensor", 'Position',[505 255 575 315]);
add_block('nesl_utility/PS-Simulink Converter', mdl + "/Vpv_To_Simulink", 'Position',[620 267 680 303]);
add_block('nesl_utility/PS-Simulink Converter', mdl + "/Ipv_To_Simulink", 'Position',[350 235 410 270]);
connect(mdl, "Vpv_Sensor", "LConn", 1, "CIN_Input_Bulk", "LConn", 1);
connect(mdl, "Vpv_Sensor", "RConn", 2, "PRI_Return_Ref", "LConn", 1);
connect(mdl, "Vpv_Sensor", "RConn", 1, "Vpv_To_Simulink", "LConn", 1);
connect(mdl, "Input_Current_Sensor", "RConn", 1, "Ipv_To_Simulink", "LConn", 1);
end

function add_measurements(mdl, mpptMode)
if mpptMode
    xShift = 1150;
else
    xShift = 800;
end
add_block('fl_lib/Electrical/Electrical Sensors/Voltage Sensor', mdl + "/Vout_Sensor", 'Position',[2345 285 2415 355] + [xShift 0 xShift 0]);
add_block('nesl_utility/PS-Simulink Converter', mdl + "/Vout_To_Simulink", 'Position',[2465 302 2525 338] + [xShift 0 xShift 0]);
add_block('nesl_utility/PS-Simulink Converter', mdl + "/Iout_To_Simulink", 'Position',[2245 420 2305 455] + [xShift 0 xShift 0]);
add_block('simulink/Sinks/Scope', mdl + "/Scope_Vout_RawLC", 'Position',[2600 260 2750 340] + [xShift 0 xShift 0]);
add_block('simulink/Sinks/To Workspace', mdl + "/Vout_scope_sl", 'Position',[2600 370 2760 400] + [xShift 0 xShift 0], 'VariableName', mdl + "_vout", 'SaveFormat','StructureWithTime');
add_block('simulink/Signal Routing/Goto', mdl + "/VOUT_MEAS_Goto", 'Position',[2555 305 2635 335] + [xShift 0 xShift 0], 'GotoTag','VOUT_MEAS', 'TagVisibility','local');
add_block('simulink/Signal Routing/Goto', mdl + "/IOUT_MEAS_Goto", 'Position',[2355 420 2435 450] + [xShift 0 xShift 0], 'GotoTag','IOUT_MEAS', 'TagVisibility','local');
set_param(mdl + "/VOUT_MEAS_Goto", 'ShowName','off');
set_param(mdl + "/IOUT_MEAS_Goto", 'ShowName','off');
connect(mdl, "Vout_Sensor", "LConn", 1, "COUT_DC_Bulk", "LConn", 1);
connect(mdl, "Vout_Sensor", "RConn", 2, "COUT_DC_Bulk", "RConn", 1);
connect(mdl, "Vout_Sensor", "RConn", 1, "Vout_To_Simulink", "LConn", 1);
connect(mdl, "Output_Current_Sensor", "RConn", 1, "Iout_To_Simulink", "LConn", 1);
add_line(mdl, 'Vout_To_Simulink/1', 'Scope_Vout_RawLC/1', 'autorouting','on');
add_line(mdl, 'Vout_To_Simulink/1', 'Vout_scope_sl/1', 'autorouting','on');
add_line(mdl, 'Vout_To_Simulink/1', 'VOUT_MEAS_Goto/1', 'autorouting','on');
add_line(mdl, 'Iout_To_Simulink/1', 'IOUT_MEAS_Goto/1', 'autorouting','on');
add_power_measurements(mdl, mpptMode, xShift);
end

function add_power_measurements(mdl, mpptMode, ~)
if mpptMode
    vinSrc = "Vpv_To_Simulink";
    iinSrc = "Ipv_To_Simulink";
    vinGotoPos = [700 270 780 300];
    iinGotoPos = [430 238 510 268];
else
    add_block('fl_lib/Electrical/Electrical Sensors/Voltage Sensor', mdl + "/Vin_Sensor", 'Position',[505 305 575 365]);
    add_block('nesl_utility/PS-Simulink Converter', mdl + "/Vin_To_Simulink", 'Position',[620 322 680 358]);
    add_block('nesl_utility/PS-Simulink Converter', mdl + "/Iin_To_Simulink", 'Position',[300 235 360 270]);
    connect(mdl, "Vin_Sensor", "LConn", 1, "CIN_Input_Bulk", "LConn", 1);
    connect(mdl, "Vin_Sensor", "RConn", 2, "CIN_Input_Bulk", "RConn", 1);
    connect(mdl, "Vin_Sensor", "RConn", 1, "Vin_To_Simulink", "LConn", 1);
    connect(mdl, "Input_Current_Sensor", "RConn", 1, "Iin_To_Simulink", "LConn", 1);
    vinSrc = "Vin_To_Simulink";
    iinSrc = "Iin_To_Simulink";
    vinGotoPos = [700 325 780 355];
    iinGotoPos = [380 238 460 268];
end

add_block('simulink/Signal Routing/Goto', mdl + "/VIN_MEAS_Goto", 'Position',vinGotoPos, 'GotoTag','VIN_MEAS', 'TagVisibility','local');
add_block('simulink/Signal Routing/Goto', mdl + "/IIN_MEAS_Goto", 'Position',iinGotoPos, 'GotoTag','IIN_MEAS', 'TagVisibility','local');
set_param(mdl + "/VIN_MEAS_Goto", 'ShowName','off');
set_param(mdl + "/IIN_MEAS_Goto", 'ShowName','off');
add_line(mdl, vinSrc + "/1", 'VIN_MEAS_Goto/1', 'autorouting','on');
add_line(mdl, iinSrc + "/1", 'IIN_MEAS_Goto/1', 'autorouting','on');

base = [930 830 930 830];
add_block('simulink/Signal Routing/From', mdl + "/VIN_MEAS_From", 'Position',base + [0 5 75 30], 'GotoTag','VIN_MEAS');
add_block('simulink/Signal Routing/From', mdl + "/IIN_MEAS_From", 'Position',base + [0 45 75 70], 'GotoTag','IIN_MEAS');
add_block('simulink/Signal Routing/From', mdl + "/VOUT_MEAS_From", 'Position',base + [0 85 75 110], 'GotoTag','VOUT_MEAS');
add_block('simulink/Signal Routing/From', mdl + "/IOUT_MEAS_From", 'Position',base + [0 125 75 150], 'GotoTag','IOUT_MEAS');
add_block('simulink/User-Defined Functions/MATLAB Function', mdl + "/Power_Efficiency_Calc", 'Position',base + [125 0 330 155]);
rt = sfroot;
chart = rt.find('-isa','Stateflow.EMChart','Path',mdl + "/Power_Efficiency_Calc");
chart.Script = power_eff_script();

add_block('simulink/Signal Routing/Mux', mdl + "/Mux_Currents", 'Position',base + [375 5 400 65], 'Inputs','2');
add_block('simulink/Signal Routing/Mux', mdl + "/Mux_Power_Eff", 'Position',base + [375 95 400 175], 'Inputs','3');
add_block('simulink/Sinks/Scope', mdl + "/Scope_Input_Output_Current", 'Position',base + [450 -5 615 75]);
add_block('simulink/Sinks/Scope', mdl + "/Scope_Power_Efficiency", 'Position',base + [450 90 615 180]);
add_block('simulink/Sinks/To Workspace', mdl + "/Vin_scope_sl", 'Position',base + [450 220 610 250], 'VariableName', mdl + "_vin", 'SaveFormat','StructureWithTime');
add_block('simulink/Sinks/To Workspace', mdl + "/Iin_scope_sl", 'Position',base + [450 260 610 290], 'VariableName', mdl + "_iin", 'SaveFormat','StructureWithTime');
add_block('simulink/Sinks/To Workspace', mdl + "/Iout_scope_sl", 'Position',base + [450 300 610 330], 'VariableName', mdl + "_iout", 'SaveFormat','StructureWithTime');
add_block('simulink/Sinks/To Workspace', mdl + "/Pin_scope_sl", 'Position',base + [635 220 795 250], 'VariableName', mdl + "_pin", 'SaveFormat','StructureWithTime');
add_block('simulink/Sinks/To Workspace', mdl + "/Pout_scope_sl", 'Position',base + [635 260 795 290], 'VariableName', mdl + "_pout", 'SaveFormat','StructureWithTime');
add_block('simulink/Sinks/To Workspace', mdl + "/Eff_scope_sl", 'Position',base + [635 300 795 330], 'VariableName', mdl + "_efficiency", 'SaveFormat','StructureWithTime');

add_line(mdl, 'VIN_MEAS_From/1', 'Power_Efficiency_Calc/1', 'autorouting','on');
add_line(mdl, 'IIN_MEAS_From/1', 'Power_Efficiency_Calc/2', 'autorouting','on');
add_line(mdl, 'VOUT_MEAS_From/1', 'Power_Efficiency_Calc/3', 'autorouting','on');
add_line(mdl, 'IOUT_MEAS_From/1', 'Power_Efficiency_Calc/4', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/1', 'Mux_Currents/1', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/2', 'Mux_Currents/2', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/3', 'Mux_Power_Eff/1', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/4', 'Mux_Power_Eff/2', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/5', 'Mux_Power_Eff/3', 'autorouting','on');
add_line(mdl, 'Mux_Currents/1', 'Scope_Input_Output_Current/1', 'autorouting','on');
add_line(mdl, 'Mux_Power_Eff/1', 'Scope_Power_Efficiency/1', 'autorouting','on');
add_line(mdl, 'VIN_MEAS_From/1', 'Vin_scope_sl/1', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/1', 'Iin_scope_sl/1', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/2', 'Iout_scope_sl/1', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/3', 'Pin_scope_sl/1', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/4', 'Pout_scope_sl/1', 'autorouting','on');
add_line(mdl, 'Power_Efficiency_Calc/5', 'Eff_scope_sl/1', 'autorouting','on');
end

function s = power_eff_script()
lines = [
"function [IinAbs,IoutAbs,Pin,Pout,Efficiency] = f(Vin,Iin,Vout,Iout)"
"%#codegen"
"IinAbs = Iin;"
"IoutAbs = Iout;"
"Pin = Vin * IinAbs;"
"Pout = Vout * Iout;"
"if Pin > 0.2"
"    Efficiency = min(max(100 * max(Pout,0) / Pin, 0), 100);"
"else"
"    Efficiency = 0;"
"end"
"end"
];
s = strjoin(lines, newline);
end

function connect(mdl, a, pa, ia, b, pb, ib)
if ~contains(string(a), "/"), a = mdl + "/" + a; end
if ~contains(string(b), "/"), b = mdl + "/" + b; end
ha = get_param(a, 'PortHandles');
hb = get_param(b, 'PortHandles');
add_line(mdl, ha.(pa)(ia), hb.(pb)(ib), 'autorouting','on');
end
