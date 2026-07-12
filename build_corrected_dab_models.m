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
stopTime = '0.01';
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
if mpptMode
    set_param(mdl + "/Rload_DC_Output", 'R','24', 'R_unit','Ohm');
    add_mppt_measurements(mdl);
end
add_measurements(mdl, mpptMode);
add_isolated_measurement_interface(mdl);
encapsulate_io_filters(mdl);
add_gate_control(mdl, mpptMode, secGan);
beautify_final_layout(mdl, secGan, mpptMode);
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
add_common_mode_choke(mdl, "LIN_Input_CM_Choke_48mm_30A", [535 275 770 505]);
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_Input_Bulk", 'Position',[675 300 735 385]);
set_param(mdl + "/CIN_Input_Bulk", 'c','4000', 'c_unit','uF', 'r','0.006', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_Input_Bulk", 'Orientation','down');
set_block_note(mdl + "/CIN_Input_Bulk", "4x NCC EKZN630ELL102ML25S: 1000uF 63V each, ESR 24mOhm@100kHz -> 4000uF, 6mOhm");
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_HF_MLCC_Bank", 'Position',[775 300 835 385]);
set_param(mdl + "/CIN_HF_MLCC_Bank", 'c','220', 'c_unit','uF', 'r','0.0010', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_HF_MLCC_Bank", 'Orientation','down');
set_block_note(mdl + "/CIN_HF_MLCC_Bank", "10x PSA FS55X226K101LRG: 22uF 100V X7R each, nominal 220uF bank");
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_X2_Film_Cap", 'Position',[875 300 935 385]);
set_param(mdl + "/CIN_X2_Film_Cap", 'c','2.2', 'c_unit','uF', 'r','0.01', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_X2_Film_Cap", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', mdl + "/RIN_Bus_Bleeder", 'Position',[970 300 1020 385]);
set_param(mdl + "/RIN_Bus_Bleeder", 'R','100000', 'R_unit','Ohm');
set_param(mdl + "/RIN_Bus_Bleeder", 'Orientation','down');
add_block('ee_lib/Passive/Resistor', mdl + "/RIN_Damping_Branch", 'Position',[620 500 685 545]);
set_param(mdl + "/RIN_Damping_Branch", 'R','0.33', 'R_unit','Ohm');
add_block('ee_lib/Passive/Capacitor', mdl + "/CIN_Damping_Cap", 'Position',[735 500 795 545]);
set_param(mdl + "/CIN_Damping_Cap", 'c','110', 'c_unit','uF', 'r','0.002', 'r_unit','Ohm', 'vc_specify','on', 'vc','24', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CIN_Damping_Cap", 'Orientation','down');
set_block_note(mdl + "/CIN_Damping_Cap", "5x PSA FS55X226K101LRG 22uF 100V X7R in damping branch; 0.33Ohm resistor dominates damping");
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
set_param(mdl + "/CSEC_DC_Link_Cap", 'c','47', 'c_unit','uF', 'r','0.036', 'r_unit','Ohm', 'max_working_voltage_range','[-100,100]', 'max_working_voltage_range_unit','V', 'vc_specify','on', 'vc','48', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/CSEC_DC_Link_Cap", 'Orientation','down');
set_block_note(mdl + "/CSEC_DC_Link_Cap", "Panasonic EEHZC1K470P: 47uF 80V hybrid polymer, ESR 36mOhm");
add_block('ee_lib/Passive/Inductor', mdl + "/LOUT_Filter_Inductor", 'Position',[1785 310 1885 360]);
set_param(mdl + "/LOUT_Filter_Inductor", 'l','2.2', 'l_unit','uH', 'r','0.0025', 'r_unit','Ohm', 'max_working_current','30', 'max_working_current_unit','A');
set_block_note(mdl + "/LOUT_Filter_Inductor", "MTP2920-2R2MC: 2.2uH, DCR 2.5mOhm, 30A rated, 100A saturation");
add_block('ee_lib/Passive/Capacitor', mdl + "/COUT_DC_Bulk", 'Position',[1950 280 2020 385]);
set_param(mdl + "/COUT_DC_Bulk", 'c','8000', 'c_unit','uF', 'r','0.003875', 'r_unit','Ohm', 'max_working_voltage_range','[-100,100]', 'max_working_voltage_range_unit','V', 'vc_specify','on', 'vc','48', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/COUT_DC_Bulk", 'Orientation','down');
set_block_note(mdl + "/COUT_DC_Bulk", "8x Suscon UR080M102J35TC97S00A: 1000uF 80V each -> 8000uF, ESR 3.875mOhm");
add_block('fl_lib/Electrical/Electrical Sensors/Current Sensor', mdl + "/Output_Current_Sensor", 'Position',[2075 300 2145 355]);
add_block('ee_lib/Passive/Capacitor', mdl + "/COUT_HF_MLCC_Cap", 'Position',[2195 405 2255 460]);
set_param(mdl + "/COUT_HF_MLCC_Cap", 'c','44', 'c_unit','uF', 'r','0.002', 'r_unit','Ohm', 'vc_specify','on', 'vc','48', 'vc_unit','V', 'vc_priority','High');
set_param(mdl + "/COUT_HF_MLCC_Cap", 'Orientation','down');
set_block_note(mdl + "/COUT_HF_MLCC_Cap", "5x PSA FS55X226K101LRG: 22uF 100V X7R, modeled as 44uF effective at 48V bias");
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

% Full bridges contain the actual nonideal MOSFET devices and gate networks.
add_full_bridge(mdl, "PRI_Full_Bridge_RealDrivers", [355 115 735 560], false, ...
    ["Q1_PRI_A_HS","Q2_PRI_A_LS","Q3_PRI_B_HS","Q4_PRI_B_LS"]);
add_full_bridge(mdl, "SEC_Full_Bridge_RealDrivers", [1240 115 1620 560], secGan, ...
    ["Q5_SEC_A_HS","Q6_SEC_A_LS","Q7_SEC_B_HS","Q8_SEC_B_LS"]);
add_selected_part_notes(mdl);

% Primary DC bus with source impedance, shunt, and EMI elements
connect(mdl, "VIN_18_32V_Bench_Source", "LConn", 1, "RSRC_Bench_Output_30mOhm", "LConn", 1);
connect(mdl, "RSRC_Bench_Output_30mOhm", "RConn", 1, "LSRC_Cable_200nH", "LConn", 1);
connect(mdl, "LSRC_Cable_200nH", "RConn", 1, "Input_Current_Sensor", "LConn", 1);
connect(mdl, "Input_Current_Sensor", "RConn", 2, "RIN_Shunt_1mOhm", "LConn", 1);
connect(mdl, "RIN_Shunt_1mOhm", "RConn", 1, "LIN_Input_CM_Choke_48mm_30A", "LConn", 1);
connect(mdl, "LIN_Input_CM_Choke_48mm_30A", "RConn", 1, "CIN_Input_Bulk", "LConn", 1);
connect(mdl, "VIN_18_32V_Bench_Source", "RConn", 1, "LIN_Input_CM_Choke_48mm_30A", "LConn", 2);
connect(mdl, "LIN_Input_CM_Choke_48mm_30A", "RConn", 2, "CIN_Input_Bulk", "RConn", 1);
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
connect(mdl, "COUT_DC_Bulk", "LConn", 1, "Output_Current_Sensor", "LConn", 1);
connect(mdl, "Output_Current_Sensor", "RConn", 2, "Rload_DC_Output", "LConn", 1);
connect(mdl, "Rload_DC_Output", "RConn", 1, "COUT_DC_Bulk", "RConn", 1);
connect(mdl, "Rload_DC_Output", "LConn", 1, "COUT_HF_MLCC_Cap", "LConn", 1);
connect(mdl, "COUT_HF_MLCC_Cap", "RConn", 1, "Rload_DC_Output", "RConn", 1);
connect(mdl, "Rload_DC_Output", "LConn", 1, "ROUT_Bleeder", "LConn", 1);
connect(mdl, "ROUT_Bleeder", "RConn", 1, "Rload_DC_Output", "RConn", 1);
connect(mdl, "Rload_DC_Output", "LConn", 1, "COUT_Y_Pos_to_PE", "LConn", 1);
connect(mdl, "COUT_Y_Pos_to_PE", "RConn", 1, "RPE_Output_Bleed", "LConn", 1);
connect(mdl, "Rload_DC_Output", "RConn", 1, "COUT_Y_Neg_to_PE", "LConn", 1);
connect(mdl, "COUT_Y_Neg_to_PE", "RConn", 1, "RPE_Output_Bleed", "LConn", 1);
connect(mdl, "RPE_Output_Bleed", "RConn", 1, "SEC_Return_Ref", "LConn", 1);
end

function set_block_note(blockPath, noteText)
try
    set_param(blockPath, 'AttributesFormatString', noteText);
catch
end
end

function add_selected_part_notes(mdl)
try
    ann = Simulink.Annotation(char(mdl), sprintf(['Selected physical parts used in the final model\n' ...
        'Q1-Q8 MOS: Alpha & Omega AONS66917, 100V, Rds_on typ 2.9mOhm, Qg 80nC.\n' ...
        'MOS isolated drivers: TI UCC21520DWR/UCC21520ADWR class, 5.7kVrms, 45ns timing budget.\n' ...
        'Input bulk: 4x NCC EKZN630ELL102ML25S, 1000uF 63V, ESR 24mOhm each.\n' ...
        'Input/output MLCC: PSA FS55X226K101LRG, 22uF 100V X7R.\n' ...
        'Secondary DC-link: Panasonic EEHZC1K470P, 47uF 80V, ESR 36mOhm.\n' ...
        'Output bulk: 8x Suscon UR080M102J35TC97S00A, total 8000uF/80V.\n' ...
        'Output power inductor: MTP2920-2R2MC, 2.2uH/30A/2.5mOhm/100A Isat.']));
    ann.Position = [120 675 940 785];
    ann.FontSize = 10;
catch
end
end

function add_common_mode_choke(mdl, blockName, pos)
cmPath = mdl + "/" + blockName;
add_block('simulink/Ports & Subsystems/Subsystem', cmPath, 'Position',pos);
try, delete_line(cmPath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(cmPath + "/In1"); catch, end
try, delete_block(cmPath + "/Out1"); catch, end
set_param(cmPath, 'Mask','on', 'MaskDisplay', ...
    "color('black');disp('48mm CM choke\n2mH, 30A, DCR<=3.5mR\nZcm>2.1k@150kHz');");
try, set_param(cmPath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(cmPath, 'MaskIconOpaque','opaque'); catch, end
try
    ann = Simulink.Annotation(char(cmPath), sprintf(['Input common-mode choke from manufacturer data\n' ...
        'Type: two-winding ferrite toroid, DC input EMI filter\n' ...
        'Core: OD 48 mm, ID 27 mm, height 16 mm, about 145 g\n' ...
        'Windings: 12 turns, 2.3 mm wire\n' ...
        'Lm per winding: 2 mH, current rating 30 A\n' ...
        'DCR max: 3.5 mOhm per winding used conservatively\n' ...
        'Common-mode impedance: > 2.1 kOhm @ 150 kHz, 0.3 V\n' ...
        'Common-mode saturation current: 200 mA']));
    ann.Position = [75 430 720 570];
    ann.FontSize = 10;
catch
end

add_block('nesl_utility/Connection Port', cmPath + "/POS_IN", 'Position',[25 95 55 125]);
add_block('nesl_utility/Connection Port', cmPath + "/NEG_IN", 'Position',[25 295 55 325]);
add_block('nesl_utility/Connection Port', cmPath + "/POS_OUT", 'Position',[760 95 790 125]);
add_block('nesl_utility/Connection Port', cmPath + "/NEG_OUT", 'Position',[760 295 790 325]);
set_param(cmPath + "/POS_IN", 'Side','Left');
set_param(cmPath + "/NEG_IN", 'Side','Left');
set_param(cmPath + "/POS_OUT", 'Side','Right');
set_param(cmPath + "/NEG_OUT", 'Side','Right');

add_block('ee_lib/Passive/Resistor', cmPath + "/Rpos_DCR", 'Position',[115 100 185 130]);
set_param(cmPath + "/Rpos_DCR", 'R','0.0035', 'R_unit','Ohm');
add_block('ee_lib/Passive/Resistor', cmPath + "/Rneg_DCR", 'Position',[115 300 185 330]);
set_param(cmPath + "/Rneg_DCR", 'R','0.0035', 'R_unit','Ohm');
add_block('fl_lib/Electrical/Electrical Elements/Mutual Inductor', cmPath + "/CM_Core_2mH", 'Position',[320 70 460 335]);
set_param(cmPath + "/CM_Core_2mH", 'L1','2', 'L1_unit','mH', 'L2','2', 'L2_unit','mH', 'k','0.9997');
add_block('ee_lib/Passive/Capacitor', cmPath + "/Cwinding_Parasitic", 'Position',[535 185 595 235]);
set_param(cmPath + "/Cwinding_Parasitic", 'c','20', 'c_unit','pF', 'r','0.1', 'r_unit','Ohm');

connect(cmPath, "POS_IN", "RConn", 1, "Rpos_DCR", "LConn", 1);
connect(cmPath, "Rpos_DCR", "RConn", 1, "CM_Core_2mH", "LConn", 1);
connect(cmPath, "CM_Core_2mH", "LConn", 2, "POS_OUT", "RConn", 1);
connect(cmPath, "NEG_IN", "RConn", 1, "Rneg_DCR", "LConn", 1);
connect(cmPath, "Rneg_DCR", "RConn", 1, "CM_Core_2mH", "RConn", 1);
connect(cmPath, "CM_Core_2mH", "RConn", 2, "NEG_OUT", "RConn", 1);
connect(cmPath, "CM_Core_2mH", "LConn", 2, "Cwinding_Parasitic", "LConn", 1);
connect(cmPath, "Cwinding_Parasitic", "RConn", 1, "CM_Core_2mH", "RConn", 2);
end

function add_full_bridge(mdl, bridgeName, pos, isGan, fetNames)
bridgePath = mdl + "/" + bridgeName;
add_block('simulink/Ports & Subsystems/Subsystem', bridgePath, 'Position',pos);
try, delete_line(bridgePath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(bridgePath + "/Out1"); catch, end
set_param(bridgePath, 'Mask','on', 'MaskDisplay',"color('black');disp('" + strrep(bridgeName,'_',' ') + "\nnonideal FETs + gate RCD');");
try, set_param(bridgePath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(bridgePath, 'MaskIconOpaque','opaque'); catch, end
set_param(bridgePath + "/In1", 'Name','G1_A_HS', 'Position',[210 135 240 155]);
add_block('simulink/Sources/In1', bridgePath + "/G2_A_LS", 'Position',[210 190 240 210]);
add_block('simulink/Sources/In1', bridgePath + "/G3_B_HS", 'Position',[210 455 240 475]);
add_block('simulink/Sources/In1', bridgePath + "/G4_B_LS", 'Position',[210 510 240 530]);
add_block('nesl_utility/Connection Port', bridgePath + "/P_BUS", 'Position',[35 55 65 85]);
add_block('nesl_utility/Connection Port', bridgePath + "/N_BUS", 'Position',[35 690 65 720]);
add_block('nesl_utility/Connection Port', bridgePath + "/SW_A", 'Position',[780 215 810 245]);
add_block('nesl_utility/Connection Port', bridgePath + "/SW_B", 'Position',[780 535 810 565]);
set_param(bridgePath + "/P_BUS", 'Side','Left');
set_param(bridgePath + "/N_BUS", 'Side','Left');
set_param(bridgePath + "/SW_A", 'Side','Right');
set_param(bridgePath + "/SW_B", 'Side','Right');

add_half_bridge(bridgePath, "Leg_A_RealSwitchingCell", [330 95 700 315], isGan, fetNames(1), fetNames(2));
add_half_bridge(bridgePath, "Leg_B_RealSwitchingCell", [330 415 700 635], isGan, fetNames(3), fetNames(4));

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
set_param(legPath + "/In1", 'Name','G_HS', 'Position',[70 125 100 145]);
add_block('simulink/Sources/In1', legPath + "/G_LS", 'Position',[70 350 100 370]);
add_block('nesl_utility/Connection Port', legPath + "/P_BUS", 'Position',[25 55 55 85]);
add_block('nesl_utility/Connection Port', legPath + "/N_BUS", 'Position',[25 470 55 500]);
add_block('nesl_utility/Connection Port', legPath + "/SW_NODE", 'Position',[775 260 805 290]);
set_param(legPath + "/P_BUS", 'Side','Left');
set_param(legPath + "/N_BUS", 'Side','Left');
set_param(legPath + "/SW_NODE", 'Side','Right');

hsPos = [515 100 605 190];
lsPos = [515 325 605 415];
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
        'C_iss','2400', 'C_iss_unit','pF', 'C_rss','90', 'C_rss_unit','pF', 'C_GS','2310', 'C_GS_unit','pF', 'C_GD','90', 'C_GD_unit','pF', 'C_oss','650', 'C_oss_unit','pF', 'C_DS','560', 'C_DS_unit','pF', 'Cj0Diode','450', 'Cj0Diode_unit','pF');
else
    set_param(blockPath, 'Rds','0.0029', 'Rds_unit','Ohm', 'RS','0.0001', 'RS_unit','Ohm', 'RD','0.0001', 'RD_unit','Ohm', 'Vth','2.2', 'Vth_unit','V', 'Rds_T2','0.0048', 'Rds_T2_unit','Ohm', ...
        'C_iss','5940', 'C_iss_unit','pF', 'C_rss','24', 'C_rss_unit','pF', 'C_GS','5916', 'C_GS_unit','pF', 'C_GD','24', 'C_GD_unit','pF', 'C_oss','1475', 'C_oss_unit','pF', 'C_DS','1451', 'C_DS_unit','pF', 'Cj0Diode','1475', 'Cj0Diode_unit','pF');
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

function add_gate_driver(mdl, fetName, fetPos, isGan)
prefix = extractBefore(fetName, "_");
if isGan
    gateHigh = 6.0; gateLow = -1.5; rgOnValue = "5.1"; rgOffValue = "1.5"; snubCValue = "680";
else
    gateHigh = 10.18; gateLow = 0.0; rgOnValue = "4.7"; rgOffValue = "2.2"; snubCValue = "470";
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
set_param(cellPath + "/In1", 'Name','GateCmd_0_10', 'Position',[30 125 60 145]);

add_block('nesl_utility/Connection Port', cellPath + "/D", 'Position',[800 70 830 100]);
add_block('nesl_utility/Connection Port', cellPath + "/G", 'Position',[800 225 830 255]);
add_block('nesl_utility/Connection Port', cellPath + "/S", 'Position',[800 385 830 415]);
set_param(cellPath + "/D", 'Side','Right');
set_param(cellPath + "/G", 'Side','Right');
set_param(cellPath + "/S", 'Side','Right');
add_block('simulink/Math Operations/Gain', cellPath + "/GateScale", 'Position',[120 117 170 153], 'Gain',num2str(gateGain,'%.4g'));
add_block('simulink/Math Operations/Bias', cellPath + "/GateBias", 'Position',[215 117 265 153], 'Bias',num2str(gateLow,'%.4g'));
add_block('nesl_utility/Simulink-PS Converter', cellPath + "/Gate_PS", 'Position',[310 118 365 152]);
set_param(cellPath + "/Gate_PS", 'Unit','V');
add_block('fl_lib/Electrical/Electrical Sources/Controlled Voltage Source', cellPath + "/Driver_Vgs_Source", 'Position',[430 135 505 225]);
add_block('ee_lib/Passive/Resistor', cellPath + "/UCC_Rout_Source_1p15Ohm", 'Position',[520 150 590 180]);
set_param(cellPath + "/UCC_Rout_Source_1p15Ohm", 'R','1.15', 'R_unit','Ohm');
add_block('ee_lib/Passive/Resistor', cellPath + "/UCC_Rout_Sink_0p55Ohm", 'Position',[420 325 490 355]);
set_param(cellPath + "/UCC_Rout_Sink_0p55Ohm", 'R','0.55', 'R_unit','Ohm');
add_block('ee_lib/Passive/Resistor', cellPath + "/Rg_On", 'Position',[585 178 665 208]);
set_param(cellPath + "/Rg_On", 'R',rgOnValue, 'R_unit','Ohm');
add_block('ee_lib/Passive/Resistor', cellPath + "/Rg_Off", 'Position',[585 292 665 322]);
set_param(cellPath + "/Rg_Off", 'R',rgOffValue, 'R_unit','Ohm');
add_block('ee_lib/Semiconductors & Converters/Diode', cellPath + "/FastOff_Diode", 'Position',[470 292 520 322]);
set_param(cellPath + "/FastOff_Diode", 'ModelType','ee.enum.diode.modelType.pwl', ...
    'Vf','0.30', 'Vf_unit','V', 'Ron','0.19', 'Ron_unit','Ohm', ...
    'BV','40', 'BV_unit','V', 'ZenerEnable','false');
add_block('ee_lib/Passive/Resistor', cellPath + "/Gate_Pulldown", 'Position',[700 280 760 360]);
set_param(cellPath + "/Gate_Pulldown", 'R','10000', 'R_unit','Ohm');
set_param(cellPath + "/Gate_Pulldown", 'Orientation','down');
add_block('ee_lib/Semiconductors & Converters/Diode', cellPath + "/Zgs_BZT52C15", 'Position',[700 365 750 395]);
set_param(cellPath + "/Zgs_BZT52C15", 'ModelType','ee.enum.diode.modelType.pwl', ...
    'Vf','0.60', 'Vf_unit','V', 'Ron','0.30', 'Ron_unit','Ohm', ...
    'ZenerEnable','true', 'BV','15', 'BV_unit','V', 'Rz','1', 'Rz_unit','Ohm');
add_block('ee_lib/Passive/Resistor', cellPath + "/DS_Snubber_R", 'Position',[535 460 615 490]);
set_param(cellPath + "/DS_Snubber_R", 'R','6.8', 'R_unit','Ohm');
add_block('ee_lib/Passive/Capacitor', cellPath + "/DS_Snubber_C", 'Position',[705 430 765 500]);
set_param(cellPath + "/DS_Snubber_C", 'c',snubCValue, 'c_unit','pF', 'r','0.05', 'r_unit','Ohm');
set_param(cellPath + "/DS_Snubber_C", 'Orientation','down');

add_line(cellPath, 'GateCmd_0_10/1', 'GateScale/1', 'autorouting','on');
add_line(cellPath, 'GateScale/1', 'GateBias/1', 'autorouting','on');
add_line(cellPath, 'GateBias/1', 'Gate_PS/1', 'autorouting','on');
connect(cellPath, "Gate_PS", "RConn", 1, "Driver_Vgs_Source", "RConn", 1);
connect(cellPath, "Driver_Vgs_Source", "LConn", 1, "UCC_Rout_Source_1p15Ohm", "LConn", 1);
connect(cellPath, "UCC_Rout_Source_1p15Ohm", "RConn", 1, "Rg_On", "LConn", 1);
connect(cellPath, "Rg_On", "RConn", 1, "G", "RConn", 1);
connect(cellPath, "G", "RConn", 1, "Rg_Off", "LConn", 1);
connect(cellPath, "Rg_Off", "RConn", 1, "FastOff_Diode", "LConn", 1);
connect(cellPath, "FastOff_Diode", "RConn", 1, "UCC_Rout_Sink_0p55Ohm", "LConn", 1);
connect(cellPath, "UCC_Rout_Sink_0p55Ohm", "RConn", 1, "Driver_Vgs_Source", "LConn", 1);
connect(cellPath, "G", "RConn", 1, "Gate_Pulldown", "LConn", 1);
connect(cellPath, "Gate_Pulldown", "RConn", 1, "S", "RConn", 1);
connect(cellPath, "S", "RConn", 1, "Zgs_BZT52C15", "LConn", 1);
connect(cellPath, "Zgs_BZT52C15", "RConn", 1, "G", "RConn", 1);
connect(cellPath, "Driver_Vgs_Source", "RConn", 2, "S", "RConn", 1);
connect(cellPath, "D", "RConn", 1, "DS_Snubber_R", "LConn", 1);
connect(cellPath, "DS_Snubber_R", "RConn", 1, "DS_Snubber_C", "LConn", 1);
connect(cellPath, "DS_Snubber_C", "RConn", 1, "S", "RConn", 1);

connect(mdl, cellName, "RConn", 1, fetName, "RConn", 1);
connect(mdl, cellName, "RConn", 2, fetName, "LConn", 1);
connect(mdl, cellName, "RConn", 3, fetName, "RConn", 2);
end

function add_isolated_driver_bank(mdl, secGan)
bankPath = mdl + "/Isolated_Gate_Drive_Interface";
add_block('simulink/Ports & Subsystems/Subsystem', bankPath, 'Position',[760 815 1045 1135]);
try, delete_line(bankPath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(bankPath + "/Out1"); catch, end
if secGan
    titleText = "RA8P1 PWM interface to secondary bridge driver inputs";
    delayText = "3.7e-8";
    note = ['Alternate secondary bridge version: PWM interface remains explicit.' newline ...
        'Primary MOSFET bridges: UCC21520-class isolated gate drivers.' newline ...
        'Modeled delay: 37 ns nominal budget, small enough for 300 kHz with 120 ns RA8P1 dead-time.' newline ...
        'No firmware architecture change: same phase-shift outputs, only driver delay/dead-time calibration differs.'];
else
    titleText = "UCC21520 isolated gate driver bank";
    delayText = "4.5e-8";
    note = ['MOSFET baseline: four UCC21520DWR-class isolated dual-channel gate drivers.' newline ...
        'Isolation rating target: 5.7 kVrms reinforced, above requested >=1500 Vrms.' newline ...
        'Modeled delay: 45 ns max-class budget; outputs remain low through UVLO/disable in hardware design.' newline ...
        'Each half bridge uses one dual-channel isolated driver plus isolated bias supply and local decoupling.' newline ...
        'RA8P1 only produces simple complementary PWM with dead-time and phase shift.'];
end
set_param(bankPath, 'Mask','on', 'MaskDisplay',"color('black');disp('" + titleText + "');");
try, set_param(bankPath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(bankPath, 'MaskIconOpaque','opaque'); catch, end
try
    ann = Simulink.Annotation(char(bankPath), note);
    ann.Position = [80 455 820 575];
    ann.FontSize = 10;
catch
end

for i = 1:8
    if i == 1
        set_param(bankPath + "/In1", 'Name',"PWM_IN_" + i, 'Position',[30 35+45*i 60 55+45*i]);
    else
        add_block('simulink/Sources/In1', bankPath + "/PWM_IN_" + i, 'Position',[30 35+45*i 60 55+45*i]);
    end
    add_block('simulink/Sinks/Out1', bankPath + "/GATE_OUT_" + i, 'Position',[725 35+45*i 755 55+45*i]);
end

driverNames = ["U1_Primary_A","U2_Primary_B","U3_Secondary_A","U4_Secondary_B"];
for d = 1:4
    y = 85 + (d-1)*95;
    add_isolated_dual_driver(bankPath, driverNames(d), [260 y 610 y+82], delayText, secGan && d >= 3);
    inA = 2*d - 1;
    inB = 2*d;
    add_line(bankPath, "PWM_IN_" + inA + "/1", driverNames(d) + "/1", 'autorouting','on');
    add_line(bankPath, "PWM_IN_" + inB + "/1", driverNames(d) + "/2", 'autorouting','on');
    add_line(bankPath, driverNames(d) + "/1", "GATE_OUT_" + inA + "/1", 'autorouting','on');
    add_line(bankPath, driverNames(d) + "/2", "GATE_OUT_" + inB + "/1", 'autorouting','on');
end
end

function add_isolated_dual_driver(parent, driverName, pos, delayText, secGanDriver)
drvPath = parent + "/" + driverName;
add_block('simulink/Ports & Subsystems/Subsystem', drvPath, 'Position',pos);
try, delete_line(drvPath, 'In1/1', 'Out1/1'); catch, end
try, delete_block(drvPath + "/Out1"); catch, end
set_param(drvPath + "/In1", 'Name','PWM_A', 'Position',[25 28 55 48]);
add_block('simulink/Sources/In1', drvPath + "/PWM_B", 'Position',[25 72 55 92]);
add_block('simulink/Sinks/Out1', drvPath + "/GATE_A", 'Position',[500 28 530 48]);
add_block('simulink/Sinks/Out1', drvPath + "/GATE_B", 'Position',[500 72 530 92]);
if secGanDriver
    icon = "Secondary bridge driver interface";
else
    icon = "UCC21520 dual isolated gate driver";
end
set_param(drvPath, 'Mask','on', 'MaskDisplay',"color('black');disp('" + icon + "');");
try, set_param(drvPath, 'ContentPreviewEnabled','off'); catch, end
try, set_param(drvPath, 'MaskIconOpaque','opaque'); catch, end
for ch = 1:2
    y = 22 + (ch-1)*44;
    add_block('simulink/Continuous/Transport Delay', drvPath + "/IsoDelay_" + ch, 'Position',[115 y 180 y+28], 'DelayTime', delayText);
    add_block('simulink/Discontinuities/Saturation', drvPath + "/LogicClamp_" + ch, 'Position',[245 y 315 y+28], 'UpperLimit','10', 'LowerLimit','0');
end
add_line(drvPath, 'PWM_A/1', 'IsoDelay_1/1', 'autorouting','on');
add_line(drvPath, 'IsoDelay_1/1', 'LogicClamp_1/1', 'autorouting','on');
add_line(drvPath, 'LogicClamp_1/1', 'GATE_A/1', 'autorouting','on');
add_line(drvPath, 'PWM_B/1', 'IsoDelay_2/1', 'autorouting','on');
add_line(drvPath, 'IsoDelay_2/1', 'LogicClamp_2/1', 'autorouting','on');
add_line(drvPath, 'LogicClamp_2/1', 'GATE_B/1', 'autorouting','on');
end

function add_isolated_measurement_interface(mdl)
iface = mdl + "/Isolated_VI_Sensing_Interface";
add_block('simulink/Ports & Subsystems/Subsystem', iface, 'Position',[2450 640 2740 835]);
try, delete_line(iface, 'In1/1', 'Out1/1'); catch, end
try, delete_block(iface + "/Out1"); catch, end
set_param(iface, 'Mask','on', 'MaskDisplay',"color('black');disp('AMC3330/AMC3302\nisolated V/I sensing');");
try, set_param(iface, 'ContentPreviewEnabled','off'); catch, end
try, set_param(iface, 'MaskIconOpaque','opaque'); catch, end
try
    ann = Simulink.Annotation(char(iface), sprintf(['Isolated measurement chain used by RA8P1 feedback\n' ...
        'Voltage: AMC3330 + precision divider, +/-1V input, gain 2, integrated isolated DC/DC\n' ...
        'Current: AMC3302 + 1mOhm Kelvin shunt, +/-50mV input, gain 41, integrated isolated DC/DC\n' ...
        'Outputs are engineering-unit values after calibration/inverse scaling, including isolation delay and clipping limits.']));
    ann.Position = [70 410 780 520];
    ann.FontSize = 10;
catch
end

set_param(iface + "/In1", 'Name','VIN_RAW', 'Position',[35 55 65 75]);
add_block('simulink/Sources/In1', iface + "/IIN_RAW", 'Position',[35 145 65 165]);
add_block('simulink/Sources/In1', iface + "/VOUT_RAW", 'Position',[35 235 65 255]);
add_block('simulink/Sources/In1', iface + "/IOUT_RAW", 'Position',[35 325 65 345]);
add_isolated_measurement_channel(iface, "VIN", 55, "0", "80");
add_isolated_measurement_channel(iface, "IIN", 145, "-80", "80");
add_isolated_measurement_channel(iface, "VOUT", 235, "0", "80");
add_isolated_measurement_channel(iface, "IOUT", 325, "-80", "80");
add_block('simulink/Sinks/Out1', iface + "/VIN_ISO", 'Position',[735 55 765 75]);
add_block('simulink/Sinks/Out1', iface + "/IIN_ISO", 'Position',[735 145 765 165]);
add_block('simulink/Sinks/Out1', iface + "/VOUT_ISO", 'Position',[735 235 765 255]);
add_block('simulink/Sinks/Out1', iface + "/IOUT_ISO", 'Position',[735 325 765 345]);

add_line(iface, 'VIN_RAW/1', 'VIN_ISO_GAIN/1', 'autorouting','on');
add_line(iface, 'VIN_ISO_GAIN/1', 'VIN_ISO_DELAY/1', 'autorouting','on');
add_line(iface, 'VIN_ISO_DELAY/1', 'VIN_ISO_CLIP/1', 'autorouting','on');
add_line(iface, 'VIN_ISO_CLIP/1', 'VIN_ISO/1', 'autorouting','on');
add_line(iface, 'IIN_RAW/1', 'IIN_ISO_GAIN/1', 'autorouting','on');
add_line(iface, 'IIN_ISO_GAIN/1', 'IIN_ISO_DELAY/1', 'autorouting','on');
add_line(iface, 'IIN_ISO_DELAY/1', 'IIN_ISO_CLIP/1', 'autorouting','on');
add_line(iface, 'IIN_ISO_CLIP/1', 'IIN_ISO/1', 'autorouting','on');
add_line(iface, 'VOUT_RAW/1', 'VOUT_ISO_GAIN/1', 'autorouting','on');
add_line(iface, 'VOUT_ISO_GAIN/1', 'VOUT_ISO_DELAY/1', 'autorouting','on');
add_line(iface, 'VOUT_ISO_DELAY/1', 'VOUT_ISO_CLIP/1', 'autorouting','on');
add_line(iface, 'VOUT_ISO_CLIP/1', 'VOUT_ISO/1', 'autorouting','on');
add_line(iface, 'IOUT_RAW/1', 'IOUT_ISO_GAIN/1', 'autorouting','on');
add_line(iface, 'IOUT_ISO_GAIN/1', 'IOUT_ISO_DELAY/1', 'autorouting','on');
add_line(iface, 'IOUT_ISO_DELAY/1', 'IOUT_ISO_CLIP/1', 'autorouting','on');
add_line(iface, 'IOUT_ISO_CLIP/1', 'IOUT_ISO/1', 'autorouting','on');

rewire_isolated_measurement_top(mdl);
end

function add_isolated_measurement_channel(parent, prefix, y, lo, hi)
add_block('simulink/Math Operations/Gain', parent + "/" + prefix + "_ISO_GAIN", ...
    'Position',[145 y-15 205 y+15], 'Gain','1.0');
add_block('simulink/Continuous/Transport Delay', parent + "/" + prefix + "_ISO_DELAY", ...
    'Position',[285 y-15 360 y+15], 'DelayTime','2e-7');
add_block('simulink/Discontinuities/Saturation', parent + "/" + prefix + "_ISO_CLIP", ...
    'Position',[445 y-15 520 y+15], 'UpperLimit',hi, 'LowerLimit',lo);
end

function rewire_isolated_measurement_top(mdl)
try, delete_line(mdl, 'Vin_To_Simulink/1', 'VIN_MEAS_Goto/1'); catch, end
try, delete_line(mdl, 'Iin_To_Simulink/1', 'IIN_MEAS_Goto/1'); catch, end
try, delete_line(mdl, 'Vout_To_Simulink/1', 'Scope_Vout_Load/1'); catch, end
try, delete_line(mdl, 'Vout_To_Simulink/1', 'Vout_scope_sl/1'); catch, end
try, delete_line(mdl, 'Vout_To_Simulink/1', 'VOUT_MEAS_Goto/1'); catch, end
try, delete_line(mdl, 'Iout_To_Simulink/1', 'IOUT_MEAS_Goto/1'); catch, end

add_line(mdl, 'Vin_To_Simulink/1', 'Isolated_VI_Sensing_Interface/1', 'autorouting','on');
add_line(mdl, 'Iin_To_Simulink/1', 'Isolated_VI_Sensing_Interface/2', 'autorouting','on');
add_line(mdl, 'Vout_To_Simulink/1', 'Isolated_VI_Sensing_Interface/3', 'autorouting','on');
add_line(mdl, 'Iout_To_Simulink/1', 'Isolated_VI_Sensing_Interface/4', 'autorouting','on');
add_line(mdl, 'Isolated_VI_Sensing_Interface/1', 'VIN_MEAS_Goto/1', 'autorouting','on');
add_line(mdl, 'Isolated_VI_Sensing_Interface/2', 'IIN_MEAS_Goto/1', 'autorouting','on');
add_line(mdl, 'Isolated_VI_Sensing_Interface/3', 'VOUT_MEAS_Goto/1', 'autorouting','on');
add_line(mdl, 'Isolated_VI_Sensing_Interface/3', 'Scope_Vout_Load/1', 'autorouting','on');
add_line(mdl, 'Isolated_VI_Sensing_Interface/3', 'Vout_scope_sl/1', 'autorouting','on');
add_line(mdl, 'Isolated_VI_Sensing_Interface/4', 'IOUT_MEAS_Goto/1', 'autorouting','on');
end

function add_gate_control(mdl, mpptMode, secGan)
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
    chartName = "RA8P1_Realtime_Voltage_PI";
    chartPath = mdl + "/" + chartName;
    add_block('simulink/User-Defined Functions/MATLAB Function', chartPath, 'Position',[350 830 610 995]);
end
rt = sfroot;
chart = rt.find('-isa','Stateflow.EMChart','Path',chartPath);
if mpptMode
    chart.Script = mppt_gate_script();
else
    chart.Script = gate_script(secGan);
end
chart.ChartUpdate = 'DISCRETE';
chart.SampleTime = '1e-7';
add_isolated_driver_bank(mdl, secGan);
for i = 1:8
    add_block('simulink/Signal Routing/Goto', mdl + "/Q" + i + "_GATE_Goto", 'Position',[1090 790+35*i 1180 815+35*i], 'GotoTag',"Q" + i + "_GATE", 'TagVisibility','local');
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
    add_line(mdl, chartName + "/" + i, "Isolated_Gate_Drive_Interface/" + i, 'autorouting','on');
    add_line(mdl, "Isolated_Gate_Drive_Interface/" + i, "Q" + i + "_GATE_Goto/1", 'autorouting','on');
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

function s = gate_script(secGan)
lines = [
"function [g1,g2,g3,g4,g5,g6,g7,g8,PhaseUs] = f(t,Vout,VrefCmd,Vin,Iout)"
"%#codegen"
"persistent phaseSec integ lastTick vFilt vinFilt iFilt"
"if isempty(phaseSec)"
"    phaseSec = 0;"
"    integ = 0;"
"    lastTick = -1;"
"    vFilt = 0;"
"    vinFilt = 24;"
"    iFilt = 0;"
"end"
"fs = 300e3;"
"T = 1/fs;"
"dead = 120e-9;"
"TsCtrl = 10e-6;"
"tick = floor(t/TsCtrl);"
"Vref = min(max(VrefCmd,36),48);"
"if tick ~= lastTick"
"    if lastTick < 0"
"        vFilt = Vout;"
"        vinFilt = max(Vin,1);"
"        iFilt = abs(Iout);"
"    else"
"        vFilt = vFilt + 0.18*(Vout - vFilt);"
"        vinFilt = vinFilt + 0.12*(max(Vin,1) - vinFilt);"
"        iFilt = iFilt + 0.35*(abs(Iout) - iFilt);"
"    end"
"    vErr = Vref - vFilt;"
"    vinUsed = min(max(vinFilt,18),32);"
"    vsecEq = Vref/2;"
"    buckNorm = min(max((vinUsed - vsecEq)/(0.25*vinUsed), 0), 1);"
"    buckBias = -0.400e-6/(1 + exp(-10*(buckNorm - 0.32)));"
"    matchBoost = max(0, 1 - 8*buckNorm);"
"    boostBias = 0.091e-6*matchBoost;"
"    iBase = 100 / Vref;"
"    iDelta = iFilt - iBase;"
"    if iDelta < 0"
"        if buckNorm > 0.55"
"            loadFF = 0.023e-6*iFilt;"
"        else"
"            negScale = min(max((0.55 - buckNorm)/0.22, 0), 1);"
"            loadFF = 0.020e-6*iDelta*negScale;"
"        end"
"    elseif iDelta <= 4"
"        loadFF = 0.065e-6*iDelta;"
"    else"
"        loadFF = min(0.313e-6, 0.242e-6 + 0.018e-6*(iDelta - 4));"
"    end"
"    if iDelta > 0"
"        if iDelta <= 7"
"            buckLoadFF = 0.0237e-6*iDelta;"
"        else"
"            buckLoadFF = min(0.660e-6, 0.172e-6 + 0.120e-6*(iDelta - 7));"
"        end"
"        buckBlend = min(max((buckNorm - 0.25)/0.45, 0), 1);"
"        loadFF = buckBlend*buckLoadFF + (1 - buckBlend)*loadFF;"
"    end"
"    if buckNorm < 0.05 && iFilt > 1.2 && iFilt < 3.0"
"        loadFF = loadFF + min(0.040e-6, 0.040e-6*(iFilt - 1.2));"
"    end"
"    midBuckTrim = -0.095e-6*exp(-((buckNorm - 0.34)/0.14)^2);"
"    shapeTrim = 0;"
"    shapeTrim = shapeTrim - 0.010e-6*exp(-((iFilt - 1.20)/0.45)^2)*exp(-((buckNorm - 1.00)/0.35)^2);"
"    shapeTrim = shapeTrim + 0.007e-6*exp(-((buckNorm - 0.67)/0.12)^2);"
"    shapeTrim = shapeTrim + 0.001e-6*exp(-((iFilt - 1.00)/0.65)^2)*exp(-((buckNorm - 0.67)/0.12)^2);"
"    shapeTrim = shapeTrim + 0.004e-6*exp(-((iFilt - 0.32)/0.22)^2)*exp(-((buckNorm - 1.00)/0.35)^2);"
"    shapeTrim = shapeTrim + 0.008e-6*exp(-((iFilt - 1.05)/0.35)^2)*exp(-((buckNorm - 0.34)/0.13)^2);"
"    shapeTrim = shapeTrim - 0.010e-6*exp(-((iFilt - 2.20)/0.45)^2)*exp(-((buckNorm - 0.34)/0.13)^2);"
"    shapeTrim = shapeTrim - 0.003e-6*exp(-((iFilt - 1.20)/0.25)^2)*exp(-((buckNorm - 0.67)/0.12)^2);"
"    shapeTrim = shapeTrim - 0.065e-6*exp(-((iFilt - 0.42)/0.25)^2)*matchBoost;"
"    shapeTrim = shapeTrim + 0.168e-6*exp(-((iFilt - 2.05)/0.45)^2)*matchBoost;"
gan_calibration_lines(secGan)
"    phaseFF = buckBias + boostBias + midBuckTrim + loadFF + shapeTrim;"
"    phaseMin = -0.52e-6;"
"    phaseMax = 0.44e-6;"
"    if phaseSec > phaseMin && phaseSec < phaseMax"
"        integ = min(max(0.995*integ + vErr*TsCtrl, -0.0025), 0.0025);"
"    end"
"    kpPhase = 0.012e-6;"
"    kiPhase = 8e-6;"
"    if buckNorm > 0.65 && iDelta > 7"
"        kpPhase = 0.004e-6;"
"    end"
"    phaseTarget = phaseFF + kpPhase*vErr + kiPhase*integ;"
"    if iFilt > 15.5"
"        phaseTarget = phaseSec - sign(phaseSec + 1e-12)*0.040e-6;"
"    end"
"    phaseTarget = min(max(phaseTarget, phaseMin), phaseMax);"
"    maxStep = 0.022e-6;"
"    phaseSec = phaseSec + min(max(phaseTarget - phaseSec, -maxStep), maxStep);"
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
];
s = strjoin(lines, newline);
end

function lines = gan_calibration_lines(secGan)
if secGan
    lines = [
        "    shapeTrim = shapeTrim + 0.010e-6*exp(-((iDelta - 5.4)/0.9)^2)*exp(-((buckNorm - 1.00)/0.35)^2);"
        "    shapeTrim = shapeTrim - 0.013e-6*(1 - exp(-(iFilt/4)^4))*matchBoost;"
        "    shapeTrim = shapeTrim + 0.030e-6*exp(-(iFilt/3.2)^4)*(1 - matchBoost);"
        "    shapeTrim = shapeTrim + 0.030e-6*exp(-((iFilt - 0.80)/0.70)^2)*matchBoost;"
        "    shapeTrim = shapeTrim - 0.015e-6*exp(-((iFilt - 2.10)/0.45)^2)*matchBoost;"
        "    shapeTrim = shapeTrim - 0.010e-6*exp(-((iFilt - 0.35)/0.25)^2)*exp(-((buckNorm - 1.00)/0.35)^2);"
        "    shapeTrim = shapeTrim - 0.010e-6*exp(-((iFilt - 0.35)/0.35)^2)*exp(-((buckNorm - 0.67)/0.12)^2);"
        "    shapeTrim = shapeTrim + 0.010e-6*exp(-((iFilt - 2.30)/0.45)^2)*exp(-((buckNorm - 0.67)/0.12)^2);"
        "    shapeTrim = shapeTrim - 0.006e-6*exp(-((buckNorm - 0.34)/0.13)^2)*(1 - exp(-(iFilt/0.8)^4));"
        "    shapeTrim = shapeTrim - 0.010e-6*exp(-((iFilt - 1.05)/0.30)^2)*exp(-((buckNorm - 0.34)/0.13)^2);"
        "    shapeTrim = shapeTrim - 0.005e-6*exp(-((iFilt - 2.20)/0.40)^2)*exp(-((buckNorm - 0.34)/0.13)^2);"
        "    shapeTrim = shapeTrim + 0.185e-6*exp(-((iFilt - 1.02)/0.25)^2)*matchBoost;"
        "    shapeTrim = shapeTrim + 0.015e-6*exp(-((iFilt - 2.50)/0.35)^2)*exp(-((buckNorm - 1.00)/0.35)^2);"
    ];
else
    lines = strings(0,1);
end
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

function encapsulate_io_filters(mdl)
inputNames = [
    "VIN_18_32V_Bench_Source","RSRC_Bench_Output_30mOhm","LSRC_Cable_200nH", ...
    "Input_Current_Sensor","RIN_Shunt_1mOhm","LIN_Input_CM_Choke_48mm_30A", ...
    "CIN_Input_Bulk","CIN_HF_MLCC_Bank","CIN_X2_Film_Cap","RIN_Bus_Bleeder", ...
    "RIN_Damping_Branch","CIN_Damping_Cap","CIN_Y_Pos_to_PE","CIN_Y_Neg_to_PE", ...
    "RPE_Primary_Bleed","PRI_Return_Ref","Solver_Config","Vin_Sensor", ...
    "Vin_To_Simulink","Iin_To_Simulink" ...
];
create_readable_subsystem(mdl, inputNames, "Input_EMI_DC_Link");

outputNames = [
    "CSEC_DC_Link_Cap","LOUT_Filter_Inductor","COUT_DC_Bulk", ...
    "Output_Current_Sensor","Rload_DC_Output", ...
    "COUT_HF_MLCC_Cap","ROUT_Bleeder", ...
    "COUT_Y_Pos_to_PE","COUT_Y_Neg_to_PE","RPE_Output_Bleed", ...
    "SEC_Return_Ref","Vout_Sensor","Vout_To_Simulink","Iout_To_Simulink" ...
];
create_readable_subsystem(mdl, outputNames, "Output_Filter_Load");
end

function create_readable_subsystem(mdl, names, newName)
handles = [];
for i = 1:numel(names)
    h = getSimulinkBlockHandle(mdl + "/" + names(i));
    if ishandle(h)
        handles(end+1) = h; %#ok<AGROW>
    end
end
if ~isempty(handles)
    Simulink.BlockDiagram.createSubsystem(handles, 'Name', char(newName), 'MakeNameUnique','off');
end
end

function beautify_final_layout(mdl, secGan, mpptMode)
% Layout-only pass. The model is generated by script, so explicit positions keep
% the final .slx readable after every rebuild instead of relying on manual drags.

% Top power schematic: left-to-right energy flow with vertical shunt branches.
move_block(mdl, "Input_EMI_DC_Link", [60 130 420 540], "right");
move_block(mdl, "VIN_18_32V_Bench_Source", [55 285 115 355], "right");
move_block(mdl, "RSRC_Bench_Output_30mOhm", [155 300 230 330], "right");
move_block(mdl, "LSRC_Cable_200nH", [270 298 355 333], "right");
move_block(mdl, "Input_Current_Sensor", [395 285 465 345], "right");
move_block(mdl, "RIN_Shunt_1mOhm", [505 300 580 330], "right");
move_block(mdl, "LIN_Input_CM_Choke_48mm_30A", [630 225 850 515], "right");
move_block(mdl, "CIN_Input_Bulk", [900 245 960 345], "down");
move_block(mdl, "CIN_HF_MLCC_Bank", [1000 245 1060 345], "down");
move_block(mdl, "CIN_X2_Film_Cap", [1100 245 1160 345], "down");
move_block(mdl, "RIN_Bus_Bleeder", [1200 245 1260 345], "down");
move_block(mdl, "RIN_Damping_Branch", [900 535 980 565], "right");
move_block(mdl, "CIN_Damping_Cap", [1025 510 1085 595], "down");
move_block(mdl, "CIN_Y_Pos_to_PE", [230 625 290 675], "down");
move_block(mdl, "CIN_Y_Neg_to_PE", [335 625 395 675], "down");
move_block(mdl, "RPE_Primary_Bleed", [455 625 515 675], "right");
move_block(mdl, "PRI_Return_Ref", [900 445 950 495], "right");
move_block(mdl, "Solver_Config", [55 470 115 525], "right");

move_block(mdl, "PRI_Full_Bridge_RealDrivers", [560 105 880 550], "right");
move_block(mdl, "T1_HF_Transformer_Real", [1040 190 1325 455], "right");
move_block(mdl, "SEC_Full_Bridge_RealDrivers", [1480 105 1800 550], "right");
move_block(mdl, "Output_Filter_Load", [1970 130 2335 540], "right");

move_block(mdl, "CSEC_DC_Link_Cap", [2505 245 2570 350], "down");
move_block(mdl, "LOUT_Filter_Inductor", [2630 300 2730 350], "right");
move_block(mdl, "COUT_DC_Bulk", [2795 245 2865 350], "down");
move_block(mdl, "Output_Current_Sensor", [3080 205 3150 265], "right");
move_block(mdl, "Rload_DC_Output", [3225 215 3295 350], "down");
move_block(mdl, "COUT_HF_MLCC_Cap", [3385 380 3445 460], "down");
move_block(mdl, "ROUT_Bleeder", [3485 380 3545 460], "down");
move_block(mdl, "COUT_Y_Pos_to_PE", [3385 530 3445 585], "down");
move_block(mdl, "COUT_Y_Neg_to_PE", [3485 530 3545 585], "down");
move_block(mdl, "RPE_Output_Bleed", [3580 530 3640 585], "right");
move_block(mdl, "SEC_Return_Ref", [2795 450 2845 500], "right");

% Scope and sensor conversion band, kept close to the physical sensors but
% outside the power-current path.
move_block(mdl, "Vin_Sensor", [930 365 1000 430], "right");
move_block(mdl, "Vin_To_Simulink", [1045 382 1105 418], "right");
move_block(mdl, "VIN_MEAS_Goto", [1140 385 1220 415], "right");
move_block(mdl, "Iin_To_Simulink", [415 175 475 210], "right");
move_block(mdl, "IIN_MEAS_Goto", [515 175 595 205], "right");
move_block(mdl, "Vout_Sensor", [3330 230 3400 300], "right");
move_block(mdl, "Vout_To_Simulink", [3440 247 3500 283], "right");
move_block(mdl, "VOUT_MEAS_Goto", [3530 250 3610 280], "right");
move_block(mdl, "Iout_To_Simulink", [3095 330 3155 365], "right");
move_block(mdl, "IOUT_MEAS_Goto", [3190 330 3270 360], "right");
move_block(mdl, "Scope_Vout_Load", [3650 220 3810 305], "right");
move_block(mdl, "Vout_scope_sl", [3650 330 3810 360], "right");
move_block(mdl, "Isolated_VI_Sensing_Interface", [2440 160 2730 355], "right");
move_block(mdl, "VIN_MEAS_Goto", [2785 175 2865 205], "right");
move_block(mdl, "IIN_MEAS_Goto", [2785 215 2865 245], "right");
move_block(mdl, "VOUT_MEAS_Goto", [2785 255 2865 285], "right");
move_block(mdl, "IOUT_MEAS_Goto", [2785 295 2865 325], "right");
move_block(mdl, "Scope_Vout_Load", [2925 245 3085 330], "right");
move_block(mdl, "Vout_scope_sl", [2925 355 3085 385], "right");

% Control and summary measurement band below the physical circuit.
move_block(mdl, "MCU_Vout_Setpoint_V", [85 810 175 840], "right");
move_block(mdl, "Sim_Time", [85 875 115 905], "right");
move_block(mdl, "VOUT_MEAS_Control_From", [85 950 170 975], "right");
move_block(mdl, "ADC_OneCycle_Delay", [220 948 270 978], "right");
move_block(mdl, "VIN_MEAS_Control_From", [85 1000 170 1025], "right");
move_block(mdl, "ADC_Delay_Vin", [220 998 270 1028], "right");
move_block(mdl, "IOUT_MEAS_Control_From", [85 1050 170 1075], "right");
move_block(mdl, "ADC_Delay_Iout", [220 1048 270 1078], "right");
move_block(mdl, "RA8P1_Realtime_Voltage_PI", [355 820 640 1065], "right");
move_block(mdl, "Isolated_Gate_Drive_Interface", [740 790 1030 1120], "right");
move_block(mdl, "Phase_us_scope_sl", [1095 1085 1260 1115], "right");

for q = 1:8
    y = 790 + q * 35;
    move_block(mdl, "Q" + q + "_GATE_Goto", [1100 y 1185 y+25], "right");
end
move_block(mdl, "Q1_GATE_From", [455 128 535 153], "right");
move_block(mdl, "Q2_GATE_From", [455 203 535 228], "right");
move_block(mdl, "Q3_GATE_From", [455 278 535 303], "right");
move_block(mdl, "Q4_GATE_From", [455 353 535 378], "right");
move_block(mdl, "Q5_GATE_From", [1375 128 1455 153], "right");
move_block(mdl, "Q6_GATE_From", [1375 203 1455 228], "right");
move_block(mdl, "Q7_GATE_From", [1375 278 1455 303], "right");
move_block(mdl, "Q8_GATE_From", [1375 353 1455 378], "right");

move_block(mdl, "VIN_MEAS_From", [1320 805 1400 830], "right");
move_block(mdl, "IIN_MEAS_From", [1320 850 1400 875], "right");
move_block(mdl, "VOUT_MEAS_From", [1320 895 1400 920], "right");
move_block(mdl, "IOUT_MEAS_From", [1320 940 1400 965], "right");
move_block(mdl, "Power_Efficiency_Calc", [1460 795 1670 965], "right");
move_block(mdl, "Mux_Currents", [1740 810 1765 870], "right");
move_block(mdl, "Mux_Power_Eff", [1740 910 1765 990], "right");
move_block(mdl, "Scope_Input_Output_Current", [1815 800 1985 880], "right");
move_block(mdl, "Scope_Power_Efficiency", [1815 910 1985 995], "right");
move_block(mdl, "Vin_scope_sl", [1815 1030 1975 1060], "right");
move_block(mdl, "Iin_scope_sl", [1815 1070 1975 1100], "right");
move_block(mdl, "Iout_scope_sl", [1815 1110 1975 1140], "right");
move_block(mdl, "Pin_scope_sl", [2030 1030 2190 1060], "right");
move_block(mdl, "Pout_scope_sl", [2030 1070 2190 1100], "right");
move_block(mdl, "Eff_scope_sl", [2030 1110 2190 1140], "right");

if mpptMode
    move_block(mdl, "Vpv_Sensor", [930 365 1000 430], "right");
    move_block(mdl, "Vpv_To_Simulink", [1045 382 1105 418], "right");
    move_block(mdl, "Ipv_To_Simulink", [415 175 475 210], "right");
end

beautify_bridge_layout(mdl + "/PRI_Full_Bridge_RealDrivers");
beautify_bridge_layout(mdl + "/SEC_Full_Bridge_RealDrivers");
beautify_half_bridge_layout(mdl + "/PRI_Full_Bridge_RealDrivers/Leg_A_RealSwitchingCell");
beautify_half_bridge_layout(mdl + "/PRI_Full_Bridge_RealDrivers/Leg_B_RealSwitchingCell");
beautify_half_bridge_layout(mdl + "/SEC_Full_Bridge_RealDrivers/Leg_A_RealSwitchingCell");
beautify_half_bridge_layout(mdl + "/SEC_Full_Bridge_RealDrivers/Leg_B_RealSwitchingCell");
beautify_transformer_layout(mdl + "/T1_HF_Transformer_Real");
beautify_cm_choke_layout(mdl + "/LIN_Input_CM_Choke_48mm_30A");
beautify_input_filter_layout(mdl + "/Input_EMI_DC_Link");
beautify_output_filter_layout(mdl + "/Output_Filter_Load");
beautify_measurement_interface_layout(mdl + "/Isolated_VI_Sensing_Interface");

topLabel = "Power path: input EMI -> primary MOSFET full bridge -> PQ4040/PC95 transformer -> secondary " + ...
    ternary(secGan, "alternate bridge", "MOSFET bridge") + " -> output filter/load. Control and scopes are kept below the power schematic.";
add_layout_note(mdl, topLabel, [55 35 900 75], 11);
add_layout_note(mdl, "RA8P1-friendly realtime feedback control: measured Vin/Vout/Iout -> PI phase command -> isolated gate interface.", [85 735 1010 775], 10);
compact_top_labels(mdl);
move_selected_part_note(mdl);
route_all_lines(mdl);
repair_top_signal_lines(mdl);
end

function beautify_bridge_layout(bridgePath)
move_block(bridgePath, "P_BUS", [35 55 65 85], "right");
move_block(bridgePath, "N_BUS", [35 690 65 720], "right");
move_block(bridgePath, "G1_A_HS", [210 135 240 155], "right");
move_block(bridgePath, "G2_A_LS", [210 190 240 210], "right");
move_block(bridgePath, "G3_B_HS", [210 455 240 475], "right");
move_block(bridgePath, "G4_B_LS", [210 510 240 530], "right");
move_block(bridgePath, "Leg_A_RealSwitchingCell", [330 95 700 315], "right");
move_block(bridgePath, "Leg_B_RealSwitchingCell", [330 415 700 635], "right");
move_block(bridgePath, "SW_A", [780 215 810 245], "right");
move_block(bridgePath, "SW_B", [780 535 810 565], "right");
add_layout_note(bridgePath, "Left: DC bus and isolated gate inputs. Right: switch nodes to transformer. Each leg contains two real FET blocks.", [85 25 730 55], 10);
route_all_lines(bridgePath);
end

function beautify_half_bridge_layout(legPath)
move_block(legPath, "P_BUS", [25 55 55 85], "right");
move_block(legPath, "N_BUS", [25 470 55 500], "right");
move_block(legPath, "G_HS", [70 125 100 145], "right");
move_block(legPath, "G_LS", [70 350 100 370], "right");
move_block(legPath, "SW_NODE", [775 260 805 290], "right");
blocks = find_system(char(legPath), 'SearchDepth', 1, 'Type','Block');
for i = 1:numel(blocks)
    name = string(get_param(blocks{i}, 'Name'));
    if endsWith(name, "_DRV_EQ")
        if any(contains(name, ["Q1","Q3","Q5","Q7"]))
            move_block(legPath, name, [215 95 430 205], "right");
        else
            move_block(legPath, name, [215 325 430 435], "right");
        end
    elseif contains(name, "_HS")
        move_block(legPath, name, [545 100 635 190], "right");
    elseif contains(name, "_LS")
        move_block(legPath, name, [545 325 635 415], "right");
    end
end
add_layout_note(legPath, "Actual switching devices are the two N-channel MOSFET blocks; driver subsystems only model gate network parasitics.", [105 25 760 55], 10);
route_all_lines(legPath);
end

function beautify_transformer_layout(txPath)
move_block(txPath, "PRI_SW_A", [25 120 55 150], "right");
move_block(txPath, "PRI_SW_B", [25 335 55 365], "right");
move_block(txPath, "Rpri_Winding", [110 125 185 155], "right");
move_block(txPath, "Coupled_Core", [290 85 410 260], "right");
move_block(txPath, "Rsec_Winding", [500 125 575 155], "right");
move_block(txPath, "Llk_Secondary_Transfer", [625 123 725 158], "right");
move_block(txPath, "SEC_SW_A", [805 120 835 150], "right");
move_block(txPath, "SEC_SW_B", [805 335 835 365], "right");
move_block(txPath, "Cpri_Winding", [275 305 335 380], "down");
move_block(txPath, "Csec_Winding", [610 305 670 380], "down");
move_block(txPath, "Cps_Interwinding", [450 405 520 455], "right");
add_layout_note(txPath, "Manufacturer parameters: PQ4040 PC95, Np:Ns=1:2, Lm_sec=230uH, Llk_sec(total)=0.5uH, DCR=0.95/3mOhm.", [85 20 800 55], 10);
route_all_lines(txPath);
end

function beautify_cm_choke_layout(cmPath)
move_block(cmPath, "POS_IN", [25 95 55 125], "right");
move_block(cmPath, "NEG_IN", [25 315 55 345], "right");
move_block(cmPath, "Rpos_DCR", [115 100 190 130], "right");
move_block(cmPath, "Rneg_DCR", [115 320 190 350], "right");
move_block(cmPath, "CM_Core_2mH", [335 85 475 360], "right");
move_block(cmPath, "Cwinding_Parasitic", [565 210 625 270], "down");
move_block(cmPath, "POS_OUT", [760 95 790 125], "right");
move_block(cmPath, "NEG_OUT", [760 315 790 345], "right");
add_layout_note(cmPath, "48mm ferrite toroid common-mode choke: 2mH, 30A, DCR 3.5mOhm max, 12 turns, 2.3mm wire.", [75 25 720 55], 10);
route_all_lines(cmPath);
end

function beautify_input_filter_layout(inPath)
move_block(inPath, "VIN_18_32V_Bench_Source", [55 220 115 290], "right");
move_block(inPath, "RSRC_Bench_Output_30mOhm", [155 235 230 265], "right");
move_block(inPath, "LSRC_Cable_200nH", [270 232 355 267], "right");
move_block(inPath, "Input_Current_Sensor", [395 220 465 280], "right");
move_block(inPath, "RIN_Shunt_1mOhm", [505 235 580 265], "right");
move_block(inPath, "LIN_Input_CM_Choke_48mm_30A", [635 160 855 455], "right");
move_block(inPath, "CIN_Input_Bulk", [925 185 985 285], "down");
move_block(inPath, "CIN_HF_MLCC_Bank", [1030 185 1090 285], "down");
move_block(inPath, "CIN_X2_Film_Cap", [1135 185 1195 285], "down");
move_block(inPath, "RIN_Bus_Bleeder", [1240 185 1300 285], "down");
move_block(inPath, "Vin_Sensor", [925 330 995 395], "right");
move_block(inPath, "Vin_To_Simulink", [1035 347 1095 383], "right");
move_block(inPath, "Iin_To_Simulink", [395 105 455 140], "right");
move_block(inPath, "RIN_Damping_Branch", [925 500 1005 530], "right");
move_block(inPath, "CIN_Damping_Cap", [1050 475 1110 560], "down");
move_block(inPath, "CIN_Y_Pos_to_PE", [230 575 290 630], "down");
move_block(inPath, "CIN_Y_Neg_to_PE", [335 575 395 630], "down");
move_block(inPath, "RPE_Primary_Bleed", [455 575 515 630], "right");
move_block(inPath, "PRI_Return_Ref", [930 430 980 480], "right");
move_block(inPath, "Solver_Config", [55 410 115 465], "right");
add_layout_note(inPath, "Input EMI/DC-link physical network: source impedance, 30A common-mode choke, bulk/HF caps, Y caps, Vin/Iin sensors.", [55 35 1210 70], 10);
route_all_lines(inPath);
end

function beautify_output_filter_layout(outPath)
move_block(outPath, "CSEC_DC_Link_Cap", [80 185 145 290], "down");
move_block(outPath, "LOUT_Filter_Inductor", [220 235 320 285], "right");
move_block(outPath, "COUT_DC_Bulk", [390 185 460 290], "down");
move_block(outPath, "Output_Current_Sensor", [685 145 755 205], "right");
move_block(outPath, "Rload_DC_Output", [835 160 905 295], "down");
move_block(outPath, "Vout_Sensor", [950 175 1020 245], "right");
move_block(outPath, "Vout_To_Simulink", [1065 192 1125 228], "right");
move_block(outPath, "Iout_To_Simulink", [705 290 765 325], "right");
move_block(outPath, "COUT_HF_MLCC_Cap", [990 375 1050 455], "down");
move_block(outPath, "ROUT_Bleeder", [1100 375 1160 455], "down");
move_block(outPath, "COUT_Y_Pos_to_PE", [990 530 1050 585], "down");
move_block(outPath, "COUT_Y_Neg_to_PE", [1100 530 1160 585], "down");
move_block(outPath, "RPE_Output_Bleed", [1200 530 1260 585], "right");
move_block(outPath, "SEC_Return_Ref", [390 430 440 480], "right");
add_layout_note(outPath, "Output physical network: 2.2uH/30A power inductor, 8mF/80V bulk bank, DC-bias-derated HF MLCC bank, load and Vout/Iout sensors.", [55 35 1240 70], 10);
route_all_lines(outPath);
end

function beautify_measurement_interface_layout(iface)
move_block(iface, "VIN_RAW", [35 55 65 75], "right");
move_block(iface, "IIN_RAW", [35 145 65 165], "right");
move_block(iface, "VOUT_RAW", [35 235 65 255], "right");
move_block(iface, "IOUT_RAW", [35 325 65 345], "right");
move_block(iface, "VIN_ISO_GAIN", [145 40 205 70], "right");
move_block(iface, "VIN_ISO_DELAY", [285 40 360 70], "right");
move_block(iface, "VIN_ISO_CLIP", [445 40 520 70], "right");
move_block(iface, "VIN_ISO", [735 55 765 75], "right");
move_block(iface, "IIN_ISO_GAIN", [145 130 205 160], "right");
move_block(iface, "IIN_ISO_DELAY", [285 130 360 160], "right");
move_block(iface, "IIN_ISO_CLIP", [445 130 520 160], "right");
move_block(iface, "IIN_ISO", [735 145 765 165], "right");
move_block(iface, "VOUT_ISO_GAIN", [145 220 205 250], "right");
move_block(iface, "VOUT_ISO_DELAY", [285 220 360 250], "right");
move_block(iface, "VOUT_ISO_CLIP", [445 220 520 250], "right");
move_block(iface, "VOUT_ISO", [735 235 765 255], "right");
move_block(iface, "IOUT_ISO_GAIN", [145 310 205 340], "right");
move_block(iface, "IOUT_ISO_DELAY", [285 310 360 340], "right");
move_block(iface, "IOUT_ISO_CLIP", [445 310 520 340], "right");
move_block(iface, "IOUT_ISO", [735 325 765 345], "right");
add_layout_note(iface, "Explicit isolation boundary for feedback. Replace unity gains with measured calibration constants after hardware bring-up.", [70 385 780 430], 10);
route_all_lines(iface);
end

function move_block(scope, name, pos, orientation)
if contains(string(name), "/")
    blk = string(name);
else
    blk = string(scope) + "/" + string(name);
end
if ishandle(getSimulinkBlockHandle(blk))
    set_param(blk, 'Position', pos);
    if nargin >= 4 && strlength(string(orientation)) > 0
        try, set_param(blk, 'Orientation', char(orientation)); catch, end
    end
end
end

function add_layout_note(scope, text, pos, fontSize)
try
    ann = Simulink.Annotation(char(scope), char(text));
    ann.Position = pos;
    ann.FontSize = fontSize;
catch
end
end

function move_selected_part_note(mdl)
try
    anns = find_system(char(mdl), 'FindAll','on', 'Type','annotation');
    for k = 1:numel(anns)
        txt = string(get_param(anns(k), 'PlainText'));
        if contains(txt, "Selected physical parts")
            set_param(anns(k), 'Position', [55 610 1040 710]);
            set_param(anns(k), 'FontSize', 9);
        end
    end
catch
end
end

function compact_top_labels(mdl)
% Keep detailed BOM data in CSV/block masks; top-level schematic labels stay
% short so wires and parts remain readable.
compact_input_labels(mdl);
compact_input_labels(mdl + "/Input_EMI_DC_Link");
compact_output_labels(mdl);
compact_output_labels(mdl + "/Output_Filter_Load");
end

function compact_input_labels(scope)
set_compact_label(scope, "VIN_18_32V_Bench_Source", "Vin 18-32V", false);
set_compact_label(scope, "RSRC_Bench_Output_30mOhm", "30mR", false);
set_compact_label(scope, "LSRC_Cable_200nH", "200nH\n3mR", false);
set_compact_label(scope, "Input_Current_Sensor", "Iin sense", true);
set_compact_label(scope, "RIN_Shunt_1mOhm", "1mR", false);
set_compact_label(scope, "CIN_Input_Bulk", "4000uF\nESR 6mR", false);
set_compact_label(scope, "CIN_HF_MLCC_Bank", "220uF\nMLCC", false);
set_compact_label(scope, "CIN_X2_Film_Cap", "2.2uF film", false);
set_compact_label(scope, "RIN_Bus_Bleeder", "100k", false);
set_compact_label(scope, "RIN_Damping_Branch", "0.33R", false);
set_compact_label(scope, "CIN_Damping_Cap", "110uF", false);
set_compact_label(scope, "CIN_Y_Pos_to_PE", "Y 2.2nF", false);
set_compact_label(scope, "CIN_Y_Neg_to_PE", "Y 2.2nF", false);
set_compact_label(scope, "RPE_Primary_Bleed", "100MR", false);
set_compact_label(scope, "Vin_Sensor", "Vin sense", true);
end

function compact_output_labels(scope)
set_compact_label(scope, "CSEC_DC_Link_Cap", "47uF 80V\nESR 36mR", false);
set_compact_label(scope, "LOUT_Filter_Inductor", "2.2uH\n2.5mR", false);
set_compact_label(scope, "COUT_DC_Bulk", "8000uF\nESR 3.875mR", false);
set_compact_label(scope, "Output_Current_Sensor", "Iout sense", true);
set_compact_label(scope, "Rload_DC_Output", "Rload", false);
set_compact_label(scope, "COUT_HF_MLCC_Cap", "44uF effective\nMLCC", false);
set_compact_label(scope, "ROUT_Bleeder", "100k", false);
set_compact_label(scope, "COUT_Y_Pos_to_PE", "Y 2.2nF", false);
set_compact_label(scope, "COUT_Y_Neg_to_PE", "Y 2.2nF", false);
set_compact_label(scope, "RPE_Output_Bleed", "100MR", false);
set_compact_label(scope, "Vout_Sensor", "Vout sense", true);
end

function set_compact_label(mdl, name, label, showName)
blk = string(mdl) + "/" + string(name);
if ishandle(getSimulinkBlockHandle(blk))
    try, set_param(blk, 'AttributesFormatString', char(label)); catch, end
    try
        if showName
            set_param(blk, 'ShowName','on');
        else
            set_param(blk, 'ShowName','off');
        end
    catch
    end
end
end

function route_all_lines(scope)
try
    lines = find_system(char(scope), 'FindAll','on', 'Type','line');
    for k = 1:numel(lines)
        try, Simulink.BlockDiagram.routeLine(lines(k)); catch, end
    end
catch
end
end

function repair_top_signal_lines(mdl)
% Recreate short signal connections after the blocks have been aligned. A full
% top-level route pass tends to drag these lines over the power schematic.
pairs = {
    "Q1_GATE_From/1", "PRI_Full_Bridge_RealDrivers/1";
    "Q2_GATE_From/1", "PRI_Full_Bridge_RealDrivers/2";
    "Q3_GATE_From/1", "PRI_Full_Bridge_RealDrivers/3";
    "Q4_GATE_From/1", "PRI_Full_Bridge_RealDrivers/4";
    "Q5_GATE_From/1", "SEC_Full_Bridge_RealDrivers/1";
    "Q6_GATE_From/1", "SEC_Full_Bridge_RealDrivers/2";
    "Q7_GATE_From/1", "SEC_Full_Bridge_RealDrivers/3";
    "Q8_GATE_From/1", "SEC_Full_Bridge_RealDrivers/4";
    "Isolated_VI_Sensing_Interface/1", "VIN_MEAS_Goto/1";
    "Isolated_VI_Sensing_Interface/2", "IIN_MEAS_Goto/1";
    "Isolated_VI_Sensing_Interface/3", "VOUT_MEAS_Goto/1";
    "Isolated_VI_Sensing_Interface/3", "Scope_Vout_Load/1";
    "Isolated_VI_Sensing_Interface/3", "Vout_scope_sl/1";
    "Isolated_VI_Sensing_Interface/4", "IOUT_MEAS_Goto/1";
};
for k = 1:size(pairs, 1)
    try, delete_line(mdl, char(pairs{k,1}), char(pairs{k,2})); catch, end
    try, add_line(mdl, char(pairs{k,1}), char(pairs{k,2}), 'autorouting','on'); catch, end
end
end

function out = ternary(cond, a, b)
if cond
    out = a;
else
    out = b;
end
end

function add_measurements(mdl, mpptMode)
xShift = 0;
add_block('fl_lib/Electrical/Electrical Sensors/Voltage Sensor', mdl + "/Vout_Sensor", 'Position',[2345 285 2415 355] + [xShift 0 xShift 0]);
add_block('nesl_utility/PS-Simulink Converter', mdl + "/Vout_To_Simulink", 'Position',[2465 302 2525 338] + [xShift 0 xShift 0]);
add_block('nesl_utility/PS-Simulink Converter', mdl + "/Iout_To_Simulink", 'Position',[2245 420 2305 455] + [xShift 0 xShift 0]);
add_block('simulink/Sinks/Scope', mdl + "/Scope_Vout_Load", 'Position',[2600 260 2750 340] + [xShift 0 xShift 0]);
add_block('simulink/Sinks/To Workspace', mdl + "/Vout_scope_sl", 'Position',[2600 370 2760 400] + [xShift 0 xShift 0], 'VariableName', mdl + "_vout", 'SaveFormat','StructureWithTime');
add_block('simulink/Signal Routing/Goto', mdl + "/VOUT_MEAS_Goto", 'Position',[2555 305 2635 335] + [xShift 0 xShift 0], 'GotoTag','VOUT_MEAS', 'TagVisibility','local');
add_block('simulink/Signal Routing/Goto', mdl + "/IOUT_MEAS_Goto", 'Position',[2355 420 2435 450] + [xShift 0 xShift 0], 'GotoTag','IOUT_MEAS', 'TagVisibility','local');
set_param(mdl + "/VOUT_MEAS_Goto", 'ShowName','off');
set_param(mdl + "/IOUT_MEAS_Goto", 'ShowName','off');
connect(mdl, "Vout_Sensor", "LConn", 1, "Rload_DC_Output", "LConn", 1);
connect(mdl, "Vout_Sensor", "RConn", 2, "Rload_DC_Output", "RConn", 1);
connect(mdl, "Vout_Sensor", "RConn", 1, "Vout_To_Simulink", "LConn", 1);
connect(mdl, "Output_Current_Sensor", "RConn", 1, "Iout_To_Simulink", "LConn", 1);
add_line(mdl, 'Vout_To_Simulink/1', 'Scope_Vout_Load/1', 'autorouting','on');
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
