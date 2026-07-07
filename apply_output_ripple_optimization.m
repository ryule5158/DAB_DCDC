function apply_output_ripple_optimization()
%APPLY_OUTPUT_RIPPLE_OPTIMIZATION Apply final circuit-level ripple reduction.
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

set_cap(mdl, "COUT_DC_Bulk", "4700", "uF", "0.0100", ...
    "4.7mF output bulk bank, ESR modeled as 10mOhm for LC damping");
set_cap(mdl, "COUT_HF_MLCC_Cap", "110", "uF", "0.0008", ...
    "5x 22uF 100V X7R MLCC output HF bank, ESR modeled as 0.8mOhm");

apply_phase_calibration(mdl);

save_system(char(mdl), modelFile);
fprintf('OUTPUT_RIPPLE_OPTIMIZATION_APPLIED %s\n', modelFile);
end

function set_cap(mdl, blockName, capValue, capUnit, esrValue, noteText)
blk = resolve_block_path(mdl, blockName);
set_param(blk, ...
    'c', char(capValue), 'c_unit', char(capUnit), ...
    'r', char(esrValue), 'r_unit', 'Ohm', ...
    'vc_specify', 'on', 'vc_priority', 'High');
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

function blk = resolve_block_path(mdl, blockName)
matches = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, 'Name', char(blockName));
if isempty(matches)
    error('Block "%s" was not found in model "%s".', blockName, mdl);
end
blk = matches{1};
end
