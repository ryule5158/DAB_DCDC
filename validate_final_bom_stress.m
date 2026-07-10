function results = validate_final_bom_stress()
%VALIDATE_FINAL_BOM_STRESS Measure electrical stress for BOM sizing.
%
% The saved SLX is not modified. Every operating point is applied through a
% SimulationInput object, and the reported window is the second half of the
% run after the precharged output network has settled.

rootDir = fileparts(mfilename('fullpath'));
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

cases = [
    18 48 100
    24 48 100
    32 48 100
    18 48 500
    24 48 500
    32 48 500
    ];
stopTime = 0.006;
tailStart = 0.003;
rows = repmat(empty_result(), size(cases, 1), 1);
for k = 1:size(cases, 1)
    rows(k) = run_case(mdl, cases(k, :), stopTime, tailStart);
    fprintf(['BOM_STRESS Vin=%gV P=%gW Vmean=%.5fV Vpp=%.5fV ' ...
        'Iin_mean=%.3fA Iin_peak=%.3fA Iout_mean=%.3fA ' ...
        'Pin=%.2fW Pout=%.2fW eff=%.2f%% phase=%.5fus\n'], ...
        rows(k).Vin_set_V, rows(k).Ptarget_W, rows(k).Vout_mean_V, ...
        rows(k).Vout_p2p_V, rows(k).Iin_mean_A, rows(k).Iin_peak_abs_A, ...
        rows(k).Iout_mean_A, rows(k).Pin_mean_W, rows(k).Pout_mean_W, ...
        rows(k).Efficiency_pct, rows(k).Phase_mean_us);
end

results = struct2table(rows);
csvPath = fullfile(outDir, mdl + "_bom_stress_matrix.csv");
writetable(results, csvPath);
fprintf('Wrote %s\n', csvPath);
close_system(char(mdl), 0);
end

function r = run_case(mdl, cfg, stopTime, tailStart)
vinSet = cfg(1);
vref = cfg(2);
powerSet = cfg(3);
rload = vref^2 / powerSet;

in = Simulink.SimulationInput(char(mdl));
in = in.setModelParameter('StopTime', number_text(stopTime));
in = in.setModelParameter('ReturnWorkspaceOutputs', 'on');
in = in.setModelParameter('SaveFormat', 'Dataset');
in = in.setBlockParameter(resolve_block_path(mdl, "VIN_18_32V_Bench_Source"), ...
    'dc_voltage', number_text(vinSet));
in = in.setBlockParameter(char(mdl + "/MCU_Vout_Setpoint_V"), ...
    'Value', number_text(vref));
in = in.setBlockParameter(resolve_block_path(mdl, "Rload_DC_Output"), ...
    'R', number_text(rload));
for capName = ["CSEC_DC_Link_Cap", "COUT_DC_Bulk", "COUT_HF_MLCC_Cap"]
    capPath = resolve_block_path(mdl, capName);
    in = in.setBlockParameter(capPath, 'vc_specify', 'on');
    in = in.setBlockParameter(capPath, 'vc_priority', 'High');
    in = in.setBlockParameter(capPath, 'vc', number_text(vref));
    in = in.setBlockParameter(capPath, 'vc_unit', 'V');
end

tic;
out = sim(in);
wall = toc;
[tVin, vin] = get_signal(out, mdl, 'vin');
[tIin, iin] = get_signal(out, mdl, 'iin');
[tVout, vout] = get_signal(out, mdl, 'vout');
[tIout, iout] = get_signal(out, mdl, 'iout');
[tPin, pin] = get_signal(out, mdl, 'pin');
[tPout, pout] = get_signal(out, mdl, 'pout');
[tPhase, phase] = get_signal(out, mdl, 'phase_us');

idxVin = tVin >= tailStart;
idxIin = tIin >= tailStart;
idxVout = tVout >= tailStart;
idxIout = tIout >= tailStart;
idxPin = tPin >= tailStart;
idxPout = tPout >= tailStart;
idxPhase = tPhase >= tailStart;
pinMean = mean(pin(idxPin));
poutMean = mean(pout(idxPout));

r = empty_result();
r.Vin_set_V = vinSet;
r.Vref_V = vref;
r.Ptarget_W = powerSet;
r.Rload_Ohm = rload;
r.Vin_mean_V = mean(vin(idxVin));
r.Vout_mean_V = mean(vout(idxVout));
r.Vout_error_V = r.Vout_mean_V - vref;
r.Vout_p2p_V = peak2peak(vout(idxVout));
r.Iin_mean_A = mean(iin(idxIin));
r.Iin_peak_abs_A = max(abs(iin(idxIin)));
r.Iout_mean_A = mean(iout(idxIout));
r.Iout_peak_abs_A = max(abs(iout(idxIout)));
r.Pin_mean_W = pinMean;
r.Pout_mean_W = poutMean;
r.Efficiency_pct = 100 * poutMean / pinMean;
r.Phase_mean_us = mean(phase(idxPhase));
r.Ripple_pass_0p1Vpp = r.Vout_p2p_V < 0.1;
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

function [t, y] = get_signal(out, mdl, suffix)
s = out.get(char(mdl + "_" + suffix));
if isa(s, 'timeseries')
    t = s.Time(:);
    y = s.Data(:);
elseif isstruct(s) && isfield(s, 'time') && isfield(s, 'signals')
    t = s.time(:);
    y = s.signals.values(:);
else
    error('Unsupported signal format for %s.', suffix);
end
end

function value = number_text(value)
value = sprintf('%.12g', value);
end

function r = empty_result()
r = struct('Vin_set_V', NaN, 'Vref_V', NaN, 'Ptarget_W', NaN, ...
    'Rload_Ohm', NaN, 'Vin_mean_V', NaN, 'Vout_mean_V', NaN, ...
    'Vout_error_V', NaN, 'Vout_p2p_V', NaN, 'Iin_mean_A', NaN, ...
    'Iin_peak_abs_A', NaN, 'Iout_mean_A', NaN, 'Iout_peak_abs_A', NaN, ...
    'Pin_mean_W', NaN, 'Pout_mean_W', NaN, 'Efficiency_pct', NaN, ...
    'Phase_mean_us', NaN, 'Ripple_pass_0p1Vpp', false, 'Wall_s', NaN);
end
