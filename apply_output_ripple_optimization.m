function apply_output_ripple_optimization()
%APPLY_OUTPUT_RIPPLE_OPTIMIZATION Apply final long-trace ripple reduction.
%
% This script intentionally patches the final hand-validated SLX directly.
% build_corrected_dab_models.m is a generator, but it does not reproduce the
% committed hand-validated controller exactly.

rootDir = fileparts(mfilename('fullpath'));
if isempty(rootDir)
    rootDir = pwd;
end
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
modelFile = fullfile(notesDir, mdl + ".slx");

if bdIsLoaded(char(mdl))
    close_system(char(mdl), 0);
end
load_system(modelFile);

remove_pseudo_output_chokes(mdl);
set_inductor(mdl, "LOUT_Filter_Inductor", "2.2", "uH", "0.0025", ...
    "MTP2920-2R2MC: 2.2uH, DCR 2.5mOhm, 30A rated, 100A saturation");
set_cap(mdl, "COUT_DC_Bulk", "8000", "uF", "0.003875", ...
    "8x UR080M102J35TC97S00A: 8000uF, equivalent ESR 3.875mOhm");
set_cap(mdl, "COUT_HF_MLCC_Cap", "44", "uF", "0.002", ...
    "5x 22uF/100V X7R; conservative 44uF effective at 48V DC bias");
configure_power_mosfets(mdl);

apply_phase_calibration(mdl);
apply_long_trace_controller_tuning(mdl);
restore_fixed_transformer_ratio(mdl);
remove_feedback_divider_calibration(mdl);
restore_default_operating_point(mdl);

save_system(char(mdl), modelFile);
fprintf('OUTPUT_RIPPLE_OPTIMIZATION_APPLIED %s\n', modelFile);
end

function remove_pseudo_output_chokes(mdl)
outPath = char(mdl + "/Output_Filter_Load");
posName = "LOUT_EMI_Pos_Choke";
negName = "LOUT_EMI_Return_Choke";
pos = find_system(outPath, 'SearchDepth', 1, 'Name', char(posName));
neg = find_system(outPath, 'SearchDepth', 1, 'Name', char(negName));
if ~isempty(pos)
    delete_block(pos{1});
end
if ~isempty(neg)
    delete_block(neg{1});
end
ensure_physical_line(outPath, "LOUT_Filter_Inductor", "RConn", 1, ...
    "Output_Current_Sensor", "LConn", 1);
ensure_physical_line(outPath, "Rload_DC_Output", "RConn", 1, ...
    "COUT_DC_Bulk", "RConn", 1);
end

function ensure_physical_line(scope, srcName, srcSide, srcIndex, dstName, dstSide, dstIndex)
src = get_param(scope + "/" + srcName, 'PortHandles');
dst = get_param(scope + "/" + dstName, 'PortHandles');
srcHandle = src.(srcSide)(srcIndex);
dstHandle = dst.(dstSide)(dstIndex);
if get_param(srcHandle, 'Line') == -1 || get_param(dstHandle, 'Line') == -1
    add_line(scope, srcHandle, dstHandle, 'autorouting', 'on');
end
end

function configure_power_mosfets(mdl)
blocks = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'Type', 'Block');
isFet = false(size(blocks));
for k = 1:numel(blocks)
    try
        isFet(k) = contains(string(get_param(blocks{k}, 'ReferenceBlock')), ...
            "N-Channel MOSFET");
    catch
    end
end
fetBlocks = blocks(isFet);
if numel(fetBlocks) ~= 8
    error('Expected eight DAB power MOSFETs, found %d.', numel(fetBlocks));
end
for k = 1:numel(fetBlocks)
    set_param(fetBlocks{k}, 'Rds', '0.0029', 'Rds_T2', '0.0048', ...
        'C_iss', '5940', 'C_rss', '24', 'C_GS', '5916', 'C_GD', '24', ...
        'C_oss', '1475', 'C_DS', '1451', 'Cj0Diode', '1475', 'Vth', '2.2');
    try
        set_param(fetBlocks{k}, 'AttributesFormatString', ...
            'AONS66917 100V MOSFET; Rds typ 2.9mOhm, hot 4.8mOhm; Ciss 5940pF');
    catch
    end
end
end

function restore_default_operating_point(mdl)
set_param(resolve_block_path(mdl, "VIN_18_32V_Bench_Source"), 'dc_voltage', '24');
set_param(resolve_block_path(mdl, "Rload_DC_Output"), 'R', '23.04');
set_param(char(mdl + "/MCU_Vout_Setpoint_V"), 'Value', '48');
set_param(char(mdl), 'StopTime', '0.01');
end

function set_inductor(mdl, blockName, inductanceValue, inductanceUnit, resistanceValue, noteText)
blk = resolve_block_path(mdl, blockName);
set_param(blk, ...
    'l', char(inductanceValue), 'l_unit', char(inductanceUnit), ...
    'r', char(resistanceValue), 'r_unit', 'Ohm');
try
    set_param(blk, 'AttributesFormatString', char(noteText));
catch
end
end

function set_cap(mdl, blockName, capValue, capUnit, esrValue, noteText)
blk = resolve_block_path(mdl, blockName);
set_param(blk, ...
    'c', char(capValue), 'c_unit', char(capUnit), ...
    'r', char(esrValue), 'r_unit', 'Ohm', ...
    'vc_specify', 'on', 'vc_priority', 'High', ...
    'vc', '48', 'vc_unit', 'V');
try
    set_param(blk, 'AttributesFormatString', char(noteText));
catch
end
end

function apply_phase_calibration(mdl)
rt = sfroot;
chart = rt.find('-isa', 'Stateflow.EMChart', ...
    'Path', char(mdl + "/RA8P1_Realtime_Voltage_PI"));
if isempty(chart)
    error('RA8P1_Realtime_Voltage_PI chart was not found.');
end

marker = "% Output-filter calibration for finalized output network.";
script = string(chart(1).Script);
if contains(script, marker)
    fprintf('Phase calibration already present.\n');
    return;
end
oldMarker = "% Output-filter calibration for 4.7mF output bank.";
if contains(script, oldMarker)
    chart(1).Script = char(replace(script, oldMarker, marker));
    fprintf('Updated legacy output-filter calibration comment.\n');
    return;
end

oldLine = "phase = min(max(phaseUs*1e-6, -0.55e-6), 0.46e-6);";
calLines = [
    marker
    "if buckDelta > 0.6"
    "    phaseUs = phaseUs + 0.095*exp(-((Iout - 13.0)/3.0)^2)*exp(-((buckDelta - 6.0)/1.4)^2);"
    "else"
    "    phaseUs = phaseUs + 0.030*(1 - exp(-max(Iout - 7.0, 0)/2.0));"
    "end"
    oldLine
    ];
if ~contains(script, oldLine)
    error('Expected base_phase output clamp line was not found.');
end
chart(1).Script = char(replace(script, oldLine, strjoin(calLines, newline)));
end

function apply_long_trace_controller_tuning(mdl)
rt = sfroot;
chart = rt.find('-isa', 'Stateflow.EMChart', ...
    'Path', char(mdl + "/RA8P1_Realtime_Voltage_PI"));
if isempty(chart)
    error('RA8P1_Realtime_Voltage_PI chart was not found.');
end

script = string(chart(1).Script);
script = replace_line(script, ...
    "vFilt = vFilt + 0.20*(Vout - vFilt);", ...
    "vFilt = vFilt + 0.40*(Vout - vFilt);");
script = replace_line(script, ...
    "trim = 0.982*trim + 0.0017e-6*err + 0.00055e-6*dErr;", ...
    "trim = 0.965*trim + 0.0102e-6*err + 0.0033e-6*dErr;");
    script = replace_line(script, ...
        "trimLimit = 0.045e-6;", ...
        "trimLimit = 0.100e-6;");
    script = replace_line(script, ...
        "phaseUs = phaseUs + 0.030*(1 - exp(-max(Iout - 7.0, 0)/2.0));", ...
        "phaseUs = phaseUs + 0.010*(1 - exp(-max(Iout - 7.0, 0)/2.0));");

    boostMarker = "% Long-trace 48V boost settling calibration.";
if ~contains(script, boostMarker)
    oldLine = "phaseUs = phaseUs + 0.010*(1 - exp(-max(Iout - 7.0, 0)/2.0));";
    newLine = oldLine + newline + boostMarker + newline + ...
        "phaseUs = phaseUs + 0.020*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);";
    if ~contains(script, oldLine)
        error('Expected boost calibration line was not found.');
    end
    script = replace(script, oldLine, newLine);
else
    script = replace_if_present(script, ...
        "phaseUs = phaseUs + 0.015*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);", ...
        "phaseUs = phaseUs + 0.020*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);");
    script = replace_if_present(script, ...
        "phaseUs = phaseUs + 0.030*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);", ...
        "phaseUs = phaseUs + 0.020*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);");
    script = replace_if_present(script, ...
        "phaseUs = phaseUs + 0.0268*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);", ...
        "phaseUs = phaseUs + 0.020*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);");
    script = replace_if_present(script, ...
        "phaseUs = phaseUs + 0.026*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);", ...
        "phaseUs = phaseUs + 0.020*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);");
    if ~contains(script, "phaseUs = phaseUs + 0.020*exp(-(buckDelta/0.75)^2)*exp(-((Iout - 2.1)/1.0)^2);")
        error('Expected long-trace boost calibration line was not found.');
    end
end
chart(1).Script = char(script);
end

function restore_fixed_transformer_ratio(mdl)
core = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'Name', 'Coupled_Core');
if isempty(core)
    error('Transformer Coupled_Core block was not found.');
end
set_param(core{1}, 'L1', '57.5', 'L1_unit', 'uH', ...
    'L2', '230', 'L2_unit', 'uH', 'k', '0.999999');
try
    set_param(core{1}, 'AttributesFormatString', ...
        'Transformer fixed by hardware: L1=57.5uH, L2=230uH, Ns/Np=2.0000');
catch
end
t1 = char(mdl + "/T1_HF_Transformer_Real");
try
    set_param(t1, 'MaskDisplay', ...
        "color('black');disp('T1 PQ4040 PC95\nNp:Ns 1:2 fixed, Llk_sec 0.5uH\nLm_sec 230uH, DCR 0.95/3mR');");
catch
end
end

function remove_feedback_divider_calibration(mdl)
gainName = "VOUT_Feedback_Divider_Cal";
matches = find_system(char(mdl), 'SearchDepth', 1, 'Name', char(gainName));
if isempty(matches)
    return;
end
try
    delete_line(char(mdl), 'VOUT_MEAS_Control_From/1', 'VOUT_Feedback_Divider_Cal/1');
catch
end
try
    delete_line(char(mdl), 'VOUT_Feedback_Divider_Cal/1', 'ADC_OneCycle_Delay/1');
catch
end
delete_block(matches{1});
try
    add_line(char(mdl), 'VOUT_MEAS_Control_From/1', 'ADC_OneCycle_Delay/1', 'autorouting', 'on');
catch
end
end

function out = replace_if_present(in, oldText, newText)
if contains(in, oldText)
    out = replace(in, oldText, newText);
else
    out = in;
end
end

function out = replace_line(in, oldText, newText)
if contains(in, newText)
    out = in;
    return;
end
if ~contains(in, oldText)
    error('Expected controller line was not found: %s', oldText);
end
out = replace(in, oldText, newText);
end

function blk = resolve_block_path(mdl, blockName)
matches = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, 'Name', char(blockName));
if isempty(matches)
    error('Block "%s" was not found in model "%s".', blockName, mdl);
end
blk = matches{1};
end
