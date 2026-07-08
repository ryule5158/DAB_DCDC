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

set_inductor(mdl, "LOUT_Filter_Inductor", "2.2", "uH", "0.0025", ...
    "2.2uH output filter inductor, DCR modeled as 2.5mOhm for faster 48V settling");
set_cap(mdl, "COUT_DC_Bulk", "4700", "uF", "0.0100", ...
    "4.7mF output bulk bank, ESR modeled as 10mOhm for LC damping");
set_cap(mdl, "COUT_HF_MLCC_Cap", "110", "uF", "0.0008", ...
    "5x 22uF 100V X7R MLCC output HF bank, ESR modeled as 0.8mOhm");

apply_phase_calibration(mdl);
apply_long_trace_controller_tuning(mdl);
apply_feedback_divider_calibration(mdl);

save_system(char(mdl), modelFile);
fprintf('OUTPUT_RIPPLE_OPTIMIZATION_APPLIED %s\n', modelFile);
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

marker = "% Output-filter calibration for 4.7mF output bank.";
script = string(chart(1).Script);
if contains(script, marker)
    fprintf('Phase calibration already present.\n');
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

function apply_feedback_divider_calibration(mdl)
gain = 0.999530;
gainName = "VOUT_Feedback_Divider_Cal";
gainBlk = char(mdl + "/" + gainName);
adcBlk = char(mdl + "/ADC_OneCycle_Delay");
fromBlk = char(mdl + "/VOUT_MEAS_Control_From");
matches = find_system(char(mdl), 'SearchDepth', 1, 'Name', char(gainName));
if isempty(matches)
    fromPos = get_param(fromBlk, 'Position');
    adcPos = get_param(adcBlk, 'Position');
    x = round((fromPos(3) + adcPos(1)) / 2);
    y = round((fromPos(2) + adcPos(2)) / 2);
    add_block('simulink/Math Operations/Gain', gainBlk, ...
        'Gain', sprintf('%.8g', gain), ...
        'Position', [x - 24, y - 16, x + 24, y + 16]);
    phAdc = get_param(adcBlk, 'PortHandles');
    oldLine = get_param(phAdc.Inport(1), 'Line');
    if oldLine ~= -1
        delete_line(oldLine);
    end
    add_line(char(mdl), 'VOUT_MEAS_Control_From/1', 'VOUT_Feedback_Divider_Cal/1', 'autorouting', 'on');
    add_line(char(mdl), 'VOUT_Feedback_Divider_Cal/1', 'ADC_OneCycle_Delay/1', 'autorouting', 'on');
else
    set_param(matches{1}, 'Gain', sprintf('%.8g', gain));
end
try
    set_param(gainBlk, 'AttributesFormatString', ...
        'Vout feedback divider calibration: MCU ADC sees 0.999530*Vout');
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
