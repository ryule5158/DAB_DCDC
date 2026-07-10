function results = analyze_dab_mosfet_candidates()
%ANALYZE_DAB_MOSFET_CANDIDATES Compare stocked 100 V MOSFET candidates.
%
% The saved model is not modified. The final-passive candidate uses an
% 8x1000uF/80V bank, conservative DC-biased MLCC capacitance, and negligible
% differential inductance for the output EMI section that will be removed.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
outDir = fullfile(notesDir, 'final_physical_models_screenshots');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
modelFile = fullfile(notesDir, mdl + ".slx");
if bdIsLoaded(char(mdl))
    close_system(char(mdl), 0);
end
load_system(modelFile);

paths.vin = resolve_block_path(mdl, "VIN_18_32V_Bench_Source");
paths.rload = resolve_block_path(mdl, "Rload_DC_Output");
paths.lout = resolve_block_path(mdl, "LOUT_Filter_Inductor");
paths.bulk = resolve_block_path(mdl, "COUT_DC_Bulk");
paths.hf = resolve_block_path(mdl, "COUT_HF_MLCC_Cap");
paths.emiPos = resolve_block_path(mdl, "LOUT_EMI_Pos_Choke");
paths.emiNeg = resolve_block_path(mdl, "LOUT_EMI_Return_Choke");
paths.fets = find_fet_paths(mdl);

devices = [
    make_device("IPT015N10N5", 1.5, 2.7, 12000, 140, 1800, 3.0)
    make_device("AONS66917",    2.9, 4.8,  5940,  24, 1475, 2.2)
    ];
powers = [100 500];
rows = repmat(empty_result(), numel(devices) * numel(powers), 1);
rowIdx = 0;
for d = 1:numel(devices)
    for p = powers
        rowIdx = rowIdx + 1;
        rows(rowIdx) = run_case(mdl, paths, devices(d), p);
        fprintf(['MOS_CASE %-14s P=%3g W mean=%.6f V p2p=%.6f V ' ...
            'Iin=%.3f A Pin=%.2f W Pout=%.2f W eff=%.2f%% phase=%.5f us wall=%.1fs\n'], ...
            rows(rowIdx).Device, rows(rowIdx).Target_power_W, ...
            rows(rowIdx).Vout_mean_V, rows(rowIdx).Vout_p2p_V, ...
            rows(rowIdx).Iin_mean_A, rows(rowIdx).Pin_mean_W, ...
            rows(rowIdx).Pout_mean_W, rows(rowIdx).Efficiency_pct, ...
            rows(rowIdx).Phase_mean_us, rows(rowIdx).Wall_s);
    end
end

results = struct2table(rows);
outCsv = fullfile(outDir, mdl + "_mosfet_candidate_screen.csv");
writetable(results, outCsv);
fprintf('Wrote %s\n', outCsv);
close_system(char(mdl), 0);
end

function dev = make_device(name, rds_mOhm, hotRds_mOhm, ciss_pF, ...
        crss_pF, coss_pF, vth_V)
dev = struct('name', string(name), 'rds_mOhm', rds_mOhm, ...
    'hotRds_mOhm', hotRds_mOhm, 'ciss_pF', ciss_pF, ...
    'crss_pF', crss_pF, 'coss_pF', coss_pF, 'vth_V', vth_V);
end

function r = run_case(mdl, paths, dev, targetPower)
if targetPower <= 100
    stopTime = 0.010;
    tailStart = 0.005;
else
    stopTime = 0.008;
    tailStart = 0.004;
end

in = Simulink.SimulationInput(char(mdl));
in = in.setModelParameter('StopTime', number_text(stopTime), ...
    'ReturnWorkspaceOutputs', 'on', 'SaveFormat', 'Dataset');
in = in.setBlockParameter(paths.vin, 'dc_voltage', '24');
in = in.setBlockParameter(char(mdl + "/MCU_Vout_Setpoint_V"), 'Value', '48');
in = in.setBlockParameter(paths.rload, 'R', number_text(48^2 / targetPower));
in = set_inductor(in, paths.lout, 2.2, 2.5);
in = set_capacitor(in, paths.bulk, 8000, 3.875);
in = set_capacitor(in, paths.hf, 44, 2.0);
in = set_inductor(in, paths.emiPos, 0.001, 0.001);
in = set_inductor(in, paths.emiNeg, 0.001, 0.001);
in = set_device(in, paths.fets, dev);

tic;
out = sim(in);
wall = toc;
[tVout, vout] = get_signal(out, mdl, 'vout');
[tIin, iin] = get_signal(out, mdl, 'iin');
[tPin, pin] = get_signal(out, mdl, 'pin');
[tPout, pout] = get_signal(out, mdl, 'pout');
[tPhase, phase] = get_signal(out, mdl, 'phase_us');

idxV = tVout >= tailStart;
vTail = vout(idxV);
pinMean = mean(pin(tPin >= tailStart));
poutMean = mean(pout(tPout >= tailStart));

r = empty_result();
r.Device = dev.name;
r.Target_power_W = targetPower;
r.Stop_time_s = stopTime;
r.Tail_start_s = tailStart;
r.Vout_mean_V = mean(vTail);
r.Vout_error_V = r.Vout_mean_V - 48;
r.Vout_min_V = min(vTail);
r.Vout_max_V = max(vTail);
r.Vout_p2p_V = r.Vout_max_V - r.Vout_min_V;
r.Vout_ac_rms_V = rms(vTail - r.Vout_mean_V);
r.Iin_mean_A = mean(iin(tIin >= tailStart));
r.Pin_mean_W = pinMean;
r.Pout_mean_W = poutMean;
r.Efficiency_pct = 100 * poutMean / pinMean;
r.Phase_mean_us = mean(phase(tPhase >= tailStart));
r.Wall_s = wall;
end

function in = set_device(in, fetPaths, dev)
for k = 1:numel(fetPaths)
    in = in.setBlockParameter(fetPaths{k}, 'Rds', number_text(dev.rds_mOhm * 1e-3));
    in = in.setBlockParameter(fetPaths{k}, 'Rds_T2', number_text(dev.hotRds_mOhm * 1e-3));
    in = in.setBlockParameter(fetPaths{k}, 'C_iss', number_text(dev.ciss_pF));
    in = in.setBlockParameter(fetPaths{k}, 'C_rss', number_text(dev.crss_pF));
    in = in.setBlockParameter(fetPaths{k}, 'C_GS', number_text(dev.ciss_pF - dev.crss_pF));
    in = in.setBlockParameter(fetPaths{k}, 'C_GD', number_text(dev.crss_pF));
    in = in.setBlockParameter(fetPaths{k}, 'C_oss', number_text(dev.coss_pF));
    in = in.setBlockParameter(fetPaths{k}, 'C_DS', number_text(dev.coss_pF - dev.crss_pF));
    in = in.setBlockParameter(fetPaths{k}, 'Cj0Diode', number_text(dev.coss_pF));
    in = in.setBlockParameter(fetPaths{k}, 'Vth', number_text(dev.vth_V));
end
end

function paths = find_fet_paths(mdl)
blocks = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'Type', 'Block');
keep = false(size(blocks));
for k = 1:numel(blocks)
    ref = string(get_param(blocks{k}, 'ReferenceBlock'));
    keep(k) = contains(ref, "N-Channel MOSFET");
end
paths = blocks(keep);
if numel(paths) ~= 8
    error('Expected 8 power MOSFET blocks, found %d.', numel(paths));
end
end

function in = set_inductor(in, blockPath, inductance_uH, dcr_mOhm)
in = in.setBlockParameter(blockPath, 'l', number_text(inductance_uH));
in = in.setBlockParameter(blockPath, 'l_unit', 'uH');
in = in.setBlockParameter(blockPath, 'r', number_text(dcr_mOhm * 1e-3));
in = in.setBlockParameter(blockPath, 'r_unit', 'Ohm');
end

function in = set_capacitor(in, blockPath, capacitance_uF, esr_mOhm)
in = in.setBlockParameter(blockPath, 'c', number_text(capacitance_uF));
in = in.setBlockParameter(blockPath, 'c_unit', 'uF');
in = in.setBlockParameter(blockPath, 'r', number_text(esr_mOhm * 1e-3));
in = in.setBlockParameter(blockPath, 'r_unit', 'Ohm');
end

function s = number_text(x)
s = sprintf('%.12g', x);
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
    error('Unsupported signal format for %s', suffix);
end
end

function r = empty_result()
r = struct('Device', "", 'Target_power_W', NaN, 'Stop_time_s', NaN, ...
    'Tail_start_s', NaN, 'Vout_mean_V', NaN, 'Vout_error_V', NaN, ...
    'Vout_min_V', NaN, 'Vout_max_V', NaN, 'Vout_p2p_V', NaN, ...
    'Vout_ac_rms_V', NaN, 'Iin_mean_A', NaN, 'Pin_mean_W', NaN, ...
    'Pout_mean_W', NaN, 'Efficiency_pct', NaN, 'Phase_mean_us', NaN, ...
    'Wall_s', NaN);
end
