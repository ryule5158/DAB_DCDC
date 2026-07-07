function results = run_targeted_final_cases(mdl, caseMatrix, tag)
%RUN_TARGETED_FINAL_CASES Run selected Simulink physical validation cases.
% caseMatrix columns: Vin, Vref, Ptarget.

if nargin < 3
    tag = "targeted";
end
mdl = string(mdl);
tag = string(tag);
rootDir = fileparts(mfilename('fullpath'));
if isempty(rootDir)
    rootDir = pwd;
end
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
outDir = fullfile(notesDir, 'final_physical_models_screenshots');
if ~exist(outDir, 'dir')
    mkdir(outDir);
end
modelFile = fullfile(notesDir, mdl + ".slx");
if bdIsLoaded(char(mdl))
    close_system(char(mdl), 0);
end
load_system(modelFile);
set_param(char(mdl), 'StopTime', '0.00045', ...
    'ReturnWorkspaceOutputs', 'on', 'SaveFormat', 'Dataset');

rows = repmat(empty_result(), size(caseMatrix, 1), 1);
for k = 1:size(caseMatrix, 1)
    cfg.Vin = caseMatrix(k, 1);
    cfg.Vref = caseMatrix(k, 2);
    cfg.Ptarget = caseMatrix(k, 3);
    rows(k) = run_one_case(mdl, cfg);
    fprintf(['CASE %s Vin=%g Vref=%g P=%g Vmean=%.4f Verr=%.4f ' ...
        'pass=%d Vpp=%.4f Iout=%.4f phase=%.5f us wall=%.1f s\n'], ...
        mdl, rows(k).Vin_set_V, rows(k).Vref_V, rows(k).Ptarget_W, ...
        rows(k).Vout_mean_V, rows(k).Vout_error_V, rows(k).Voltage_pass_0p1, ...
        rows(k).Vout_p2p_V, rows(k).Iout_mean_A, rows(k).Phase_mean_us, rows(k).Wall_s);
end

results = struct2table(rows);
writetable(results, fullfile(outDir, mdl + "_" + tag + ".csv"));
assignin('base', char(mdl + "_" + tag), results);
end

function r = run_one_case(mdl, cfg)
set_param(resolve_block_path(mdl, "VIN_18_32V_Bench_Source"), 'dc_voltage', sprintf('%.10g', cfg.Vin));
set_param(char(mdl + "/MCU_Vout_Setpoint_V"), 'Value', sprintf('%.10g', cfg.Vref));
rload = cfg.Vref^2 / cfg.Ptarget;
set_param(resolve_block_path(mdl, "Rload_DC_Output"), 'R', sprintf('%.10g', rload));
set_output_initial_voltage(mdl, cfg.Vref);
tic;
simOut = sim(char(mdl));
wall = toc;
[tVin, vin] = get_signal(simOut, mdl, 'vin');
[tIin, iin] = get_signal(simOut, mdl, 'iin');
[tVout, vout] = get_signal(simOut, mdl, 'vout');
[tIout, iout] = get_signal(simOut, mdl, 'iout');
[tPin, pin] = get_signal(simOut, mdl, 'pin');
[tPout, pout] = get_signal(simOut, mdl, 'pout');
[tEff, eff] = get_signal(simOut, mdl, 'efficiency');
[tPh, ph] = get_signal(simOut, mdl, 'phase_us');

r = empty_result();
r.Vin_set_V = cfg.Vin;
r.Vref_V = cfg.Vref;
r.Ptarget_W = cfg.Ptarget;
r.Rload_Ohm = rload;
r.Vin_mean_V = mean(vin(tail_index(tVin)));
r.Vout_mean_V = mean(vout(tail_index(tVout)));
r.Vout_error_V = r.Vout_mean_V - cfg.Vref;
r.Voltage_pass_0p1 = abs(r.Vout_error_V) <= 0.1;
r.Vout_final_V = vout(end);
r.Vout_p2p_V = max(vout(tail_index(tVout))) - min(vout(tail_index(tVout)));
r.Iin_mean_A = mean(iin(tail_index(tIin)));
r.Iout_mean_A = mean(iout(tail_index(tIout)));
r.Iload_calc_A = r.Vout_mean_V / rload;
r.Pin_mean_W = mean(pin(tail_index(tPin)));
r.Pout_mean_W = mean(pout(tail_index(tPout)));
r.Pload_calc_W = r.Vout_mean_V^2 / rload;
r.Eff_mean_pct = mean(eff(tail_index(tEff)));
r.Phase_mean_us = mean(ph(tail_index(tPh)));
r.Wall_s = wall;
end

function set_output_initial_voltage(mdl, vref)
capNames = ["CSEC_DC_Link_Cap", "COUT_DC_Bulk", "COUT_HF_MLCC_Cap"];
for i = 1:numel(capNames)
    blk = resolve_block_path(mdl, capNames(i));
    set_param(blk, 'vc_specify', 'on', 'vc_priority', 'High', ...
        'vc', sprintf('%.10g', vref), 'vc_unit', 'V');
end
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
r = struct('Vin_set_V', NaN, 'Vref_V', NaN, 'Ptarget_W', NaN, ...
    'Rload_Ohm', NaN, 'Vin_mean_V', NaN, 'Vout_mean_V', NaN, ...
    'Vout_error_V', NaN, 'Voltage_pass_0p1', false, ...
    'Vout_final_V', NaN, 'Vout_p2p_V', NaN, 'Iin_mean_A', NaN, ...
    'Iout_mean_A', NaN, 'Iload_calc_A', NaN, 'Pin_mean_W', NaN, ...
    'Pout_mean_W', NaN, 'Pload_calc_W', NaN, 'Eff_mean_pct', NaN, ...
    'Phase_mean_us', NaN, 'Wall_s', NaN);
end

function idx = tail_index(t)
n = numel(t);
idx = max(1, floor(n*0.75)):n;
end

function [t, y] = get_signal(simOut, mdl, suffix)
varName = char(mdl + "_" + suffix);
s = simOut.get(varName);
if isa(s, 'timeseries')
    t = s.Time(:);
    y = s.Data(:);
elseif isstruct(s) && isfield(s, 'time') && isfield(s, 'signals')
    t = s.time(:);
    y = s.signals.values(:);
else
    error('Unsupported signal format for %s', varName);
end
end
