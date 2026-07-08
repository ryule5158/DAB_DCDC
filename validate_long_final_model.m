function results = validate_long_final_model(mode, repeats)
%VALIDATE_LONG_FINAL_MODEL Validate the saved final MOSFET model as-is.
%
% Unlike screening scripts, this does not patch Stateflow code or substitute
% candidate values. It loads the saved SLX and measures the long-trace output
% behavior that is visible in the Scope window.

if nargin < 1
    mode = "verify48";
end
if nargin < 2
    repeats = 3;
end
mode = string(mode);

rootDir = fileparts(mfilename('fullpath'));
if isempty(rootDir)
    rootDir = pwd;
end
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
outDir = fullfile(notesDir, 'final_physical_models_screenshots');
if ~exist(outDir, 'dir')
    mkdir(outDir);
end

mdl = "DAB_RA8P1_Final_Physical_MOSFET";
modelFile = fullfile(notesDir, mdl + ".slx");
if bdIsLoaded(char(mdl))
    close_system(char(mdl), 0);
end
load_system(modelFile);

if mode == "spot"
    cases = [
        24 48 50
        24 48 100
        24 48 300
        24 48 500
        ];
elseif mode == "exact48"
    cases = [
        24 48 100
        ];
else
    cases = [
        24 48 100
        ];
end

if mode == "exact48"
    stopTime = 0.0100;
    tailStart = 0.0090;
    errorLimit = 0.001;
    rippleLimit = 0.150;
else
    stopTime = 0.0085;
    tailStart = 0.0060;
    errorLimit = 0.050;
    rippleLimit = 0.100;
end
rows = repmat(empty_result(), size(cases, 1) * repeats, 1);
rowIdx = 0;
for rep = 1:repeats
    for k = 1:size(cases, 1)
        cfg.Vin = cases(k, 1);
        cfg.Vref = cases(k, 2);
        cfg.Ptarget = cases(k, 3);
        rowIdx = rowIdx + 1;
        rows(rowIdx) = run_one_case(mdl, cfg, stopTime, tailStart, errorLimit, rippleLimit, rep);
        fprintf(['FINAL_LONG rep=%d Vin=%g Vref=%g P=%g mean=%.5f err=%.5f ' ...
            'p2p=%.5f rms=%.5f pass=%d phase=%.5f us wall=%.1fs\n'], ...
            rows(rowIdx).Repeat, rows(rowIdx).Vin_set_V, rows(rowIdx).Vref_V, ...
            rows(rowIdx).Ptarget_W, rows(rowIdx).Vout_mean_tail_V, ...
            rows(rowIdx).Vout_error_tail_V, rows(rowIdx).Vout_p2p_tail_V, ...
            rows(rowIdx).Vout_ac_rms_tail_V, rows(rowIdx).Pass_strict_48V, ...
            rows(rowIdx).Phase_mean_tail_us, rows(rowIdx).Wall_s);
    end
end

results = struct2table(rows);
outCsv = fullfile(outDir, mdl + "_long_final_model_" + mode + ".csv");
writetable(results, outCsv);
fprintf('Wrote %s\n', outCsv);
end

function r = run_one_case(mdl, cfg, stopTime, tailStart, errorLimit, rippleLimit, repeatIdx)
rload = cfg.Vref^2 / cfg.Ptarget;
in = Simulink.SimulationInput(char(mdl));
in = in.setModelParameter('StopTime', sprintf('%.10g', stopTime));
in = in.setModelParameter('ReturnWorkspaceOutputs', 'on');
in = in.setModelParameter('SaveFormat', 'Dataset');
in = in.setBlockParameter(resolve_block_path(mdl, "VIN_18_32V_Bench_Source"), ...
    'dc_voltage', sprintf('%.10g', cfg.Vin));
in = in.setBlockParameter(char(mdl + "/MCU_Vout_Setpoint_V"), ...
    'Value', sprintf('%.10g', cfg.Vref));
in = in.setBlockParameter(resolve_block_path(mdl, "Rload_DC_Output"), ...
    'R', sprintf('%.10g', rload));
tic;
out = sim(in);
wall = toc;

[tVout, vout] = get_signal(out, mdl, 'vout');
[tPh, ph] = get_signal(out, mdl, 'phase_us');
idxV = tVout >= tailStart;
idxPh = tPh >= tailStart;
vTail = vout(idxV);

r = empty_result();
r.Repeat = repeatIdx;
r.Vin_set_V = cfg.Vin;
r.Vref_V = cfg.Vref;
r.Ptarget_W = cfg.Ptarget;
r.Rload_Ohm = rload;
r.StopTime_s = stopTime;
r.TailStart_s = tailStart;
r.Vout_mean_tail_V = mean(vTail);
r.Vout_error_tail_V = r.Vout_mean_tail_V - cfg.Vref;
r.Vout_final_V = vout(end);
r.Vout_min_tail_V = min(vTail);
r.Vout_max_tail_V = max(vTail);
r.Vout_p2p_tail_V = r.Vout_max_tail_V - r.Vout_min_tail_V;
r.Vout_ac_rms_tail_V = rms(vTail - r.Vout_mean_tail_V);
r.Phase_mean_tail_us = mean(ph(idxPh));
r.Error_limit_V = errorLimit;
r.Ripple_limit_Vpp = rippleLimit;
r.Pass_strict_48V = abs(r.Vout_error_tail_V) <= errorLimit && r.Vout_p2p_tail_V < rippleLimit;
r.Wall_s = wall;
end

function blk = resolve_block_path(mdl, blockName)
matches = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, 'Name', char(blockName));
if isempty(matches)
    error('Block "%s" was not found in model "%s".', blockName, mdl);
end
blk = matches{1};
end

function r = empty_result()
r = struct('Repeat', NaN, 'Vin_set_V', NaN, 'Vref_V', NaN, ...
    'Ptarget_W', NaN, 'Rload_Ohm', NaN, 'StopTime_s', NaN, ...
    'TailStart_s', NaN, 'Vout_mean_tail_V', NaN, ...
    'Vout_error_tail_V', NaN, 'Vout_final_V', NaN, ...
    'Vout_min_tail_V', NaN, 'Vout_max_tail_V', NaN, ...
    'Vout_p2p_tail_V', NaN, 'Vout_ac_rms_tail_V', NaN, ...
    'Phase_mean_tail_us', NaN, 'Error_limit_V', NaN, ...
    'Ripple_limit_Vpp', NaN, 'Pass_strict_48V', false, 'Wall_s', NaN);
end

function [t, y] = get_signal(out, mdl, suffix)
s = out.get(char(mdl + "_" + suffix));
if isa(s, 'timeseries')
    t = s.Time(:);
    y = s.Data(:);
elseif isstruct(s) && isfield(s, 'time') && isfield(s, 'signals')
    t = s.time(:);
    y = s.signals.values(:);
else
    error('Unsupported signal format for %s', suffix);
end
end
