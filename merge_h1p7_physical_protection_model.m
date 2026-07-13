function merge_h1p7_physical_protection_model()
%MERGE_H1P7_PHYSICAL_PROTECTION_MODEL Merge verified H1.7 improvements.
%   Keeps the current physical power stage, inserts TMCS1133 measurement
%   dynamics, worst-case hardware OC delay, a hardware-latch abstraction,
%   and independent driver-enable gates. It also installs the EPS branch
%   revalidated on this physical model. The operation is idempotent.

rootDir = fileparts(mfilename('fullpath'));
modelFile = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes', ...
    'DAB_RA8P1_Final_Physical_MOSFET.slx');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
load_system(modelFile);
cleanup = onCleanup(@() close_if_loaded(mdl));

remove_previous_merge(mdl);
disconnect_output(mdl + "/IPRI_HF_To_Simulink", 1);
disconnect_output(mdl + "/ISEC_HF_To_Simulink", 1);
disconnect_output(mdl + "/RAW_FAULT_N_Model", 1);
for q = 1:8
    disconnect_output(mdl + "/RA8P1_Realtime_Voltage_PI", q);
    disconnect_input(mdl + "/Isolated_Gate_Drive_Interface", q);
end

add_measurement_dynamics(mdl);
add_hardware_overcurrent(mdl);
add_fault_latch_and_gate_enable(mdl);
wire_measurement_and_protection(mdl);
wire_hardware_gate_enable(mdl);
set_param(mdl, 'SimulationCommand', 'update');
apply_final_safety_controller();
set_param(mdl, 'SimulationCommand', 'update');
set_param(mdl, 'Description', sprintf(['H1.7 physical DAB model. ' ...
    'Includes MOS/PCB/TMCS conductor parasitics, TMCS 120 ns + 1.1 MHz ' ...
    'measurement path, 250 ns worst-case OC delay, hardware latch ' ...
    'abstraction, independent driver enable, and physical-model EPS.']));
save_system(mdl, modelFile);
fprintf('MERGED_H1P7_PHYSICAL_PROTECTION=%s\n', modelFile);
end

function add_measurement_dynamics(mdl)
add_block('simulink/Continuous/Transport Delay', ...
    mdl + "/TMCS_IPRI_Analog_Delay", 'DelayTime', '120e-9', ...
    'Position', [5120 1410 5170 1450]);
add_block('simulink/Continuous/Transfer Fcn', ...
    mdl + "/TMCS_IPRI_1p1MHz_LPF", ...
    'Numerator', '[2*pi*1.1e6]', ...
    'Denominator', '[1 2*pi*1.1e6]', ...
    'Position', [5220 1412 5290 1448]);
add_block('simulink/Sinks/To Workspace', ...
    mdl + "/IPRI_TMCS_scope_sl", ...
    'VariableName', mdl + "_ipri_tmcs", ...
    'SaveFormat', 'StructureWithTime', ...
    'Position', [5340 1405 5450 1435]);

add_block('simulink/Continuous/Transport Delay', ...
    mdl + "/TMCS_ISEC_Analog_Delay", 'DelayTime', '120e-9', ...
    'Position', [5600 350 5650 390]);
add_block('simulink/Continuous/Transfer Fcn', ...
    mdl + "/TMCS_ISEC_1p1MHz_LPF", ...
    'Numerator', '[2*pi*1.1e6]', ...
    'Denominator', '[1 2*pi*1.1e6]', ...
    'Position', [5700 352 5770 388]);
add_block('simulink/Sinks/To Workspace', ...
    mdl + "/ISEC_TMCS_scope_sl", ...
    'VariableName', mdl + "_isec_tmcs", ...
    'SaveFormat', 'StructureWithTime', ...
    'Position', [5820 345 5930 375]);
end

function add_hardware_overcurrent(mdl)
add_block('simulink/Math Operations/Abs', ...
    mdl + "/IPRI_Abs_For_OC", 'Position', [5000 1570 5030 1600]);
add_block('simulink/Continuous/Transport Delay', ...
    mdl + "/TMCS_IPRI_OC_WorstCase_Delay", ...
    'DelayTime', '250e-9', 'Position', [5090 1565 5140 1605]);
add_block('simulink/Sources/Constant', ...
    mdl + "/IPRI_HW_OC_Threshold_A", 'Value', '65.3', ...
    'Position', [5095 1625 5140 1655]);
add_block('simulink/Logic and Bit Operations/Relational Operator', ...
    mdl + "/IPRI_OC_Safe", 'Operator', '<', ...
    'Position', [5200 1570 5235 1605]);

add_block('simulink/Math Operations/Abs', ...
    mdl + "/ISEC_Abs_For_OC", 'Position', [5000 1740 5030 1770]);
add_block('simulink/Continuous/Transport Delay', ...
    mdl + "/TMCS_ISEC_OC_WorstCase_Delay", ...
    'DelayTime', '250e-9', 'Position', [5090 1735 5140 1775]);
add_block('simulink/Sources/Constant', ...
    mdl + "/ISEC_HW_OC_Threshold_A", 'Value', '40.0', ...
    'Position', [5095 1795 5140 1825]);
add_block('simulink/Logic and Bit Operations/Relational Operator', ...
    mdl + "/ISEC_OC_Safe", 'Operator', '<', ...
    'Position', [5200 1740 5235 1775]);

add_block('simulink/Logic and Bit Operations/Logical Operator', ...
    mdl + "/RAW_FAULT_N_With_OC", 'Operator', 'AND', 'Inputs', '3', ...
    'Position', [5330 1650 5370 1700]);
add_block('simulink/Sinks/To Workspace', ...
    mdl + "/RAW_FAULT_N_scope_sl", ...
    'VariableName', mdl + "_raw_fault_n", ...
    'SaveFormat', 'StructureWithTime', ...
    'Position', [5410 1715 5525 1745]);
end

function add_fault_latch_and_gate_enable(mdl)
add_block('simulink/User-Defined Functions/MATLAB Function', ...
    mdl + "/Hardware_Fault_Latch_N", ...
    'Position', [5440 1625 5580 1685]);
rt = sfroot;
chart = rt.find('-isa', 'Stateflow.EMChart', ...
    'Path', char(mdl + "/Hardware_Fault_Latch_N"));
assert(~isempty(chart), 'Hardware fault-latch chart was not created.');
chart.Script = sprintf([ ...
    'function FaultLatchN = f(RawFaultN,RunReq)\n' ...
    '%%#codegen\n' ...
    '%% Simulates U_LATCH. Real PCB reset remains a separate MCU pulse.\n' ...
    'persistent faultLatched\n' ...
    'if isempty(faultLatched)\n' ...
    '    faultLatched = false;\n' ...
    'end\n' ...
    'if RawFaultN <= 0.5\n' ...
    '    faultLatched = true;\n' ...
    'elseif RunReq <= 0.5\n' ...
    '    faultLatched = false;\n' ...
    'end\n' ...
    'FaultLatchN = double(~faultLatched);\n' ...
    'end\n']);
chart.ChartUpdate = 'DISCRETE';
chart.SampleTime = '1e-8';

add_block('simulink/Math Operations/Product', ...
    mdl + "/HW_DRIVER_ENABLE", 'Inputs', '2', ...
    'Position', [5850 1860 5890 1900]);
add_block('simulink/Sinks/To Workspace', ...
    mdl + "/FAULT_LATCH_N_scope_sl", ...
    'VariableName', mdl + "_fault_latch_n", ...
    'SaveFormat', 'StructureWithTime', ...
    'Position', [5600 1720 5725 1750]);
add_block('simulink/Sinks/To Workspace', ...
    mdl + "/DRIVER_ENABLE_scope_sl", ...
    'VariableName', mdl + "_driver_enable", ...
    'SaveFormat', 'StructureWithTime', ...
    'Position', [5850 1810 5975 1840]);

for q = 1:8
    y = 1948 + 35*(q-1);
    add_block('simulink/Math Operations/Product', ...
        mdl + "/HW_GATE_Q" + string(q), 'Inputs', '2', ...
        'Position', [5970 y 6005 y+22]);
end
end

function wire_measurement_and_protection(mdl)
add_line(mdl, 'IPRI_HF_To_Simulink/1', 'IPRI_HF_scope_sl/1', ...
    'autorouting', 'on');
add_line(mdl, 'IPRI_HF_To_Simulink/1', 'TMCS_IPRI_Analog_Delay/1', ...
    'autorouting', 'on');
add_line(mdl, 'TMCS_IPRI_Analog_Delay/1', 'TMCS_IPRI_1p1MHz_LPF/1', ...
    'autorouting', 'on');
add_line(mdl, 'TMCS_IPRI_1p1MHz_LPF/1', ...
    'RA8P1_Realtime_Voltage_PI/6', 'autorouting', 'on');
add_line(mdl, 'TMCS_IPRI_1p1MHz_LPF/1', 'IPRI_TMCS_scope_sl/1', ...
    'autorouting', 'on');

add_line(mdl, 'ISEC_HF_To_Simulink/1', 'ISEC_HF_scope_sl/1', ...
    'autorouting', 'on');
add_line(mdl, 'ISEC_HF_To_Simulink/1', 'TMCS_ISEC_Analog_Delay/1', ...
    'autorouting', 'on');
add_line(mdl, 'TMCS_ISEC_Analog_Delay/1', 'TMCS_ISEC_1p1MHz_LPF/1', ...
    'autorouting', 'on');
add_line(mdl, 'TMCS_ISEC_1p1MHz_LPF/1', ...
    'RA8P1_Realtime_Voltage_PI/7', 'autorouting', 'on');
add_line(mdl, 'TMCS_ISEC_1p1MHz_LPF/1', 'ISEC_TMCS_scope_sl/1', ...
    'autorouting', 'on');

add_line(mdl, 'IPRI_HF_To_Simulink/1', 'IPRI_Abs_For_OC/1', ...
    'autorouting', 'on');
add_line(mdl, 'IPRI_Abs_For_OC/1', ...
    'TMCS_IPRI_OC_WorstCase_Delay/1', 'autorouting', 'on');
add_line(mdl, 'TMCS_IPRI_OC_WorstCase_Delay/1', 'IPRI_OC_Safe/1', ...
    'autorouting', 'on');
add_line(mdl, 'IPRI_HW_OC_Threshold_A/1', 'IPRI_OC_Safe/2', ...
    'autorouting', 'on');

add_line(mdl, 'ISEC_HF_To_Simulink/1', 'ISEC_Abs_For_OC/1', ...
    'autorouting', 'on');
add_line(mdl, 'ISEC_Abs_For_OC/1', ...
    'TMCS_ISEC_OC_WorstCase_Delay/1', 'autorouting', 'on');
add_line(mdl, 'TMCS_ISEC_OC_WorstCase_Delay/1', 'ISEC_OC_Safe/1', ...
    'autorouting', 'on');
add_line(mdl, 'ISEC_HW_OC_Threshold_A/1', 'ISEC_OC_Safe/2', ...
    'autorouting', 'on');

add_line(mdl, 'RAW_FAULT_N_Model/1', 'RAW_FAULT_N_With_OC/1', ...
    'autorouting', 'on');
add_line(mdl, 'IPRI_OC_Safe/1', 'RAW_FAULT_N_With_OC/2', ...
    'autorouting', 'on');
add_line(mdl, 'ISEC_OC_Safe/1', 'RAW_FAULT_N_With_OC/3', ...
    'autorouting', 'on');
add_line(mdl, 'RAW_FAULT_N_With_OC/1', 'RAW_FAULT_N_scope_sl/1', ...
    'autorouting', 'on');
add_line(mdl, 'RAW_FAULT_N_With_OC/1', 'Hardware_Fault_Latch_N/1', ...
    'autorouting', 'on');
add_line(mdl, 'RUN_REQ_Model/1', 'Hardware_Fault_Latch_N/2', ...
    'autorouting', 'on');
add_line(mdl, 'Hardware_Fault_Latch_N/1', ...
    'RA8P1_Realtime_Voltage_PI/9', 'autorouting', 'on');
add_line(mdl, 'Hardware_Fault_Latch_N/1', ...
    'FAULT_LATCH_N_scope_sl/1', 'autorouting', 'on');
end

function wire_hardware_gate_enable(mdl)
add_line(mdl, 'RUN_REQ_Model/1', 'HW_DRIVER_ENABLE/1', 'autorouting', 'on');
add_line(mdl, 'Hardware_Fault_Latch_N/1', 'HW_DRIVER_ENABLE/2', ...
    'autorouting', 'on');
add_line(mdl, 'HW_DRIVER_ENABLE/1', 'DRIVER_ENABLE_scope_sl/1', ...
    'autorouting', 'on');
for q = 1:8
    qText = string(q);
    add_line(mdl, "RA8P1_Realtime_Voltage_PI/" + qText, ...
        "HW_GATE_Q" + qText + "/1", 'autorouting', 'on');
    add_line(mdl, 'HW_DRIVER_ENABLE/1', "HW_GATE_Q" + qText + "/2", ...
        'autorouting', 'on');
    add_line(mdl, "HW_GATE_Q" + qText + "/1", ...
        "Isolated_Gate_Drive_Interface/" + qText, 'autorouting', 'on');
end
end

function install_physical_eps_branch(mdl)
rt = sfroot;
chart = rt.find('-isa', 'Stateflow.EMChart', ...
    'Path', char(mdl + "/RA8P1_Realtime_Voltage_PI"));
assert(~isempty(chart), 'RA8P1 controller chart was not found.');
script = string(chart.Script);
if contains(script, 'Physical-model EPS')
    if ~contains(script, 'epsPhase = min(max(0.305e-6 + trim')
        apply_final_safety_controller();
        chart = rt.find('-isa', 'Stateflow.EMChart', ...
            'Path', char(mdl + "/RA8P1_Realtime_Voltage_PI"));
    end
    chart.ChartUpdate = 'DISCRETE';
    chart.SampleTime = '1e-8';
    return;
end
old = strjoin([
    "if normalEnabled && faultOk"
    "    phase = normalRamp*(base_phase(Vref, vinFilt, iFilt) + trim);"
    "    phase = min(max(phase, -0.55e-6), 0.46e-6);"
    "    [a,an] = halfbridge(t,T,deadTime);"
    "    [b,bn] = halfbridge(t+T/2,T,deadTime);"
    "    [c,cn] = halfbridge(t-phase,T,deadTime);"
    "    [d,dn] = halfbridge(t-phase+T/2,T,deadTime);"
    "    g1 = 10*a;  g2 = 10*an;"
    "    g3 = 10*b;  g4 = 10*bn;"
    "    g5 = 10*c;  g6 = 10*cn;"
    "    g7 = 10*d;  g8 = 10*dn;"
    "    PhaseUs = phase*1e6;"
    ], newline);
new = strjoin([
    "if normalEnabled && faultOk && iFilt >= 4"
    "    % Physical-model EPS PI around a 0.305 us measured feed-forward point."
    "    % The final phase is limited and quantized to the 10 ns PWM timer."
    "    if iPriPeakHold < 50 && iSecPeakHold < 28"
    "        inner = 0.20e-6;"
    "        epsPhase = min(max(0.305e-6 + trim,0.28e-6),0.34e-6);"
    "        phase = round(epsPhase/10e-9)*10e-9;"
    "        [a,an] = halfbridge(t,T,deadTime);"
    "        [b,bn] = halfbridge(t+T/2-inner,T,deadTime);"
    "        [c,cn] = halfbridge(t-phase,T,deadTime);"
    "        [d,dn] = halfbridge(t-phase+T/2,T,deadTime);"
    "        g1 = 10*a;  g2 = 10*an;"
    "        g3 = 10*b;  g4 = 10*bn;"
    "        g5 = 10*c;  g6 = 10*cn;"
    "        g7 = 10*d;  g8 = 10*dn;"
    "        PhaseUs = phase*1e6;"
    "    else"
    "        g1 = 0; g2 = 0; g3 = 0; g4 = 0;"
    "        g5 = 0; g6 = 0; g7 = 0; g8 = 0;"
    "        PhaseUs = 0;"
    "    end"
    "elseif normalEnabled && faultOk"
    "    phase = normalRamp*(base_phase(Vref, vinFilt, iFilt) + trim);"
    "    phase = min(max(phase, -0.55e-6), 0.46e-6);"
    "    [a,an] = halfbridge(t,T,deadTime);"
    "    [b,bn] = halfbridge(t+T/2,T,deadTime);"
    "    [c,cn] = halfbridge(t-phase,T,deadTime);"
    "    [d,dn] = halfbridge(t-phase+T/2,T,deadTime);"
    "    g1 = 10*a;  g2 = 10*an;"
    "    g3 = 10*b;  g4 = 10*bn;"
    "    g5 = 10*c;  g6 = 10*cn;"
    "    g7 = 10*d;  g8 = 10*dn;"
    "    PhaseUs = phase*1e6;"
    ], newline);
assert(count(script, old) == 1, ...
    'Expected exactly one unmodified NORMAL_SPS output branch.');
chart.Script = char(replace(script, old, new));
chart.ChartUpdate = 'DISCRETE';
chart.SampleTime = '1e-8';
end

function remove_previous_merge(mdl)
names = [
    "TMCS_IPRI_Analog_Delay"
    "TMCS_IPRI_1p1MHz_LPF"
    "IPRI_TMCS_scope_sl"
    "TMCS_ISEC_Analog_Delay"
    "TMCS_ISEC_1p1MHz_LPF"
    "ISEC_TMCS_scope_sl"
    "IPRI_Abs_For_OC"
    "TMCS_IPRI_OC_WorstCase_Delay"
    "IPRI_HW_OC_Threshold_A"
    "IPRI_OC_Safe"
    "ISEC_Abs_For_OC"
    "TMCS_ISEC_OC_WorstCase_Delay"
    "ISEC_HW_OC_Threshold_A"
    "ISEC_OC_Safe"
    "RAW_FAULT_N_With_OC"
    "RAW_FAULT_N_scope_sl"
    "Hardware_Fault_Latch_N"
    "HW_DRIVER_ENABLE"
    "FAULT_LATCH_N_scope_sl"
    "DRIVER_ENABLE_scope_sl"
    ];
names = [names; "HW_GATE_Q" + string((1:8).')];
for name = names.'
    path = mdl + "/" + name;
    if getSimulinkBlockHandle(path) >= 0
        delete_block(path);
    end
end
end

function disconnect_output(block, portNumber)
ports = get_param(block, 'PortHandles');
if numel(ports.Outport) < portNumber
    return;
end
line = get_param(ports.Outport(portNumber), 'Line');
if line >= 0
    delete_line(line);
end
end

function disconnect_input(block, portNumber)
ports = get_param(block, 'PortHandles');
if numel(ports.Inport) < portNumber
    return;
end
line = get_param(ports.Inport(portNumber), 'Line');
if line >= 0
    delete_line(line);
end
end

function close_if_loaded(mdl)
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
end
