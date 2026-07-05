function results = run_final_physical_validation(mode)
%RUN_FINAL_PHYSICAL_VALIDATION Run acceptance cases on the final physical DAB SLX.
%
% Results are read from Simscape sensor paths already wired to Scope/To
% Workspace blocks in DAB_RA8P1_Final_Physical_MOSFET. This file does not
% implement a replacement plant model.

if nargin < 1
    mode = "all";
end

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
outDir = fullfile(notesDir, 'final_physical_mosfet_screenshots');
if ~exist(outDir, 'dir')
    mkdir(outDir);
end

mdl = 'DAB_RA8P1_Final_Physical_MOSFET';
modelFile = fullfile(notesDir, [mdl '.slx']);
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
load_system(modelFile);

set_param(mdl, ...
    'StopTime', '0.00045', ...
    'ReturnWorkspaceOutputs', 'on', ...
    'SaveFormat', 'Dataset');
set_param([mdl '/VIN_18_32V_Bench_Source'], 'dc_voltage', '24');

cases = build_cases(string(mode));
rows = repmat(empty_result(), numel(cases), 1);
for k = 1:numel(cases)
    rows(k) = run_one_case(mdl, cases(k));
    fprintf(['CASE Vin=%g Vref=%g Ptarget=%g R=%g ' ...
        'Vmean=%.3f Vfinal=%.3f Pout=%.2f Pin=%.2f ' ...
        'Iout=%.3f phase=%.4f us wall=%.1f s\n'], ...
        rows(k).Vin_set_V, rows(k).Vref_V, rows(k).Ptarget_W, ...
        rows(k).Rload_Ohm, rows(k).Vout_mean_V, rows(k).Vout_final_V, ...
        rows(k).Pout_mean_W, rows(k).Pin_mean_W, rows(k).Iout_mean_A, ...
        rows(k).Phase_mean_us, rows(k).Wall_s);
end

results = struct2table(rows);
csvPath = fullfile(outDir, "final_physical_validation_" + string(mode) + ".csv");
writetable(results, csvPath);
plotPath = fullfile(outDir, "final_physical_validation_" + string(mode) + ".png");
plot_validation_summary(results, plotPath);

fprintf('FINAL_PHYSICAL_VALIDATION_CSV=%s\n', csvPath);
fprintf('FINAL_PHYSICAL_VALIDATION_PNG=%s\n', plotPath);
disp(results);
set_param(mdl, 'Dirty', 'off');
end

function cases = build_cases(mode)
switch mode
    case "48v_power"
        targets = [20 50 100 300 500];
        cases = make_cases(24, 48, targets);
    case "common_matrix"
        cases = struct('Vin', {}, 'Vref', {}, 'Ptarget', {});
        vrefs = [36 40 44 48];
        targets = [20 50 100];
        for v = vrefs
            cases = [cases make_cases(24, v, targets)]; %#ok<AGROW>
        end
    case "high_power"
        cases = [make_cases(24, 36, [300 500]) make_cases(24, 48, [300 500])];
    case "all"
        cases = [build_cases("common_matrix") build_cases("high_power")];
    otherwise
        error('Unknown validation mode: %s', mode);
end
end

function cases = make_cases(vin, vref, targets)
cases = repmat(struct('Vin', vin, 'Vref', vref, 'Ptarget', targets(1)), numel(targets), 1);
for i = 1:numel(targets)
    cases(i).Vin = vin;
    cases(i).Vref = vref;
    cases(i).Ptarget = targets(i);
end
end

function r = run_one_case(mdl, cfg)
set_param([mdl '/VIN_18_32V_Bench_Source'], 'dc_voltage', sprintf('%.10g', cfg.Vin));
set_param([mdl '/MCU_Vout_Setpoint_V'], 'Value', sprintf('%.10g', cfg.Vref));
rload = cfg.Vref^2 / cfg.Ptarget;
set_param([mdl '/Rload_DC_Output'], 'R', sprintf('%.10g', rload));
set_output_initial_voltage(mdl, cfg.Vref);

tic;
simOut = sim(mdl);
wall = toc;

[tVin, vin] = get_signal(simOut, mdl, 'vin');
[tIin, iin] = get_signal(simOut, mdl, 'iin');
[tVout, vout] = get_signal(simOut, mdl, 'vout');
[tIout, iout] = get_signal(simOut, mdl, 'iout');
[tPin, pin] = get_signal(simOut, mdl, 'pin');
[tPout, pout] = get_signal(simOut, mdl, 'pout');
[tEff, eff] = get_signal(simOut, mdl, 'efficiency');
[tPh, ph] = get_signal(simOut, mdl, 'phase_us');

idxVin = tail_index(tVin);
idxIin = tail_index(tIin);
idxVout = tail_index(tVout);
idxIout = tail_index(tIout);
idxPin = tail_index(tPin);
idxPout = tail_index(tPout);
idxEff = tail_index(tEff);
idxPh = tail_index(tPh);

r = empty_result();
r.Vin_set_V = cfg.Vin;
r.Vref_V = cfg.Vref;
r.Ptarget_W = cfg.Ptarget;
r.Rload_Ohm = rload;
r.Vin_mean_V = mean(vin(idxVin));
r.Vout_mean_V = mean(vout(idxVout));
r.Vout_final_V = vout(end);
r.Vout_p2p_V = max(vout(idxVout)) - min(vout(idxVout));
r.Iin_mean_A = mean(iin(idxIin));
r.Iout_mean_A = mean(iout(idxIout));
r.Pin_mean_W = mean(pin(idxPin));
r.Pout_mean_W = mean(pout(idxPout));
r.Eff_mean_pct = mean(eff(idxEff));
r.Phase_mean_us = mean(ph(idxPh));
r.Wall_s = wall;
end

function set_output_initial_voltage(mdl, vref)
capNames = ["CSEC_DC_Link_Cap", "COUT_DC_Bulk", "COUT_X_Film_Cap"];
for i = 1:numel(capNames)
    blk = [mdl '/' char(capNames(i))];
    set_param(blk, 'vc_specify', 'on', 'vc_priority', 'High', ...
        'vc', sprintf('%.10g', vref), 'vc_unit', 'V');
end
end

function r = empty_result()
r = struct( ...
    'Vin_set_V', NaN, ...
    'Vref_V', NaN, ...
    'Ptarget_W', NaN, ...
    'Rload_Ohm', NaN, ...
    'Vin_mean_V', NaN, ...
    'Vout_mean_V', NaN, ...
    'Vout_final_V', NaN, ...
    'Vout_p2p_V', NaN, ...
    'Iin_mean_A', NaN, ...
    'Iout_mean_A', NaN, ...
    'Pin_mean_W', NaN, ...
    'Pout_mean_W', NaN, ...
    'Eff_mean_pct', NaN, ...
    'Phase_mean_us', NaN, ...
    'Wall_s', NaN);
end

function idx = tail_index(t)
n = numel(t);
idx = max(1, floor(n*0.75)):n;
end

function [t, y] = get_signal(simOut, mdl, suffix)
varName = [mdl '_' suffix];
s = simOut.get(varName);
if isa(s, 'timeseries')
    t = s.Time(:);
    y = s.Data(:);
elseif isstruct(s) && isfield(s, 'time') && isfield(s, 'signals')
    t = s.time(:);
    y = s.signals.values;
    y = y(:);
else
    error('Unsupported signal format for %s', varName);
end
end

function plot_validation_summary(results, plotPath)
fig = figure('Visible', 'off', 'Color', 'w', 'Position', [80 80 1450 850]);
tiledlayout(fig, 2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

nexttile;
scatter(results.Ptarget_W, results.Vout_mean_V, 42, results.Vref_V, 'filled');
hold on;
for v = unique(results.Vref_V).'
    yline(v, '--', sprintf('%g V', v), 'LabelHorizontalAlignment', 'left');
end
grid on;
xlabel('Target power (W)');
ylabel('Mean Vout from sensor (V)');
title('Voltage regulation');
colorbar;

nexttile;
scatter(results.Ptarget_W, results.Pout_mean_W, 42, results.Vref_V, 'filled');
hold on;
plot([0 max(results.Ptarget_W)], [0 max(results.Ptarget_W)], 'k--');
grid on;
xlabel('Target power (W)');
ylabel('Mean Pout from sensor calc (W)');
title('Output power');
colorbar;

nexttile;
scatter(results.Ptarget_W, results.Iout_mean_A, 42, results.Vref_V, 'filled');
grid on;
xlabel('Target power (W)');
ylabel('Mean Iout (A)');
title('Current');
colorbar;

nexttile;
scatter(results.Ptarget_W, results.Phase_mean_us, 42, results.Vref_V, 'filled');
grid on;
xlabel('Target power (W)');
ylabel('Phase command (us)');
title('RA8P1-simple phase command');
colorbar;

exportgraphics(fig, plotPath, 'Resolution', 160);
close(fig);
end
