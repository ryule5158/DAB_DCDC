function results = analyze_dab_filter_candidates()
%ANALYZE_DAB_FILTER_CANDIDATES Screen physical output-filter candidates.
%
% The saved SLX is not modified. Each candidate is applied through
% Simulink.SimulationInput so the comparison uses the same controller and
% power stage. The two output EMI inductors are represented by their
% differential-mode leakage during this scalar screening; the selected
% design can then be implemented as a coupled common-mode choke.

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

paths.vin = resolve_block_path(mdl, "VIN_18_32V_Bench_Source");
paths.rload = resolve_block_path(mdl, "Rload_DC_Output");
paths.lout = resolve_block_path(mdl, "LOUT_Filter_Inductor");
paths.bulk = resolve_block_path(mdl, "COUT_DC_Bulk");
paths.hf = resolve_block_path(mdl, "COUT_HF_MLCC_Cap");
paths.emiPos = resolve_block_path(mdl, "LOUT_EMI_Pos_Choke");
paths.emiNeg = resolve_block_path(mdl, "LOUT_EMI_Return_Choke");

cases = build_cases();
rows = repmat(empty_result(), numel(cases), 1);
for k = 1:numel(cases)
    rows(k) = run_candidate(mdl, paths, cases(k));
    fprintf(['FILTER_CASE %-24s mean=%.6f V err=%+.6f V p2p=%.6f V ' ...
        'ac_rms=%.6f V fdom=%.1f Hz Iin=%.3f A Pin=%.2f W Pout=%.2f W wall=%.1fs\n'], ...
        rows(k).Candidate, rows(k).Vout_mean_V, rows(k).Vout_error_V, ...
        rows(k).Vout_p2p_V, rows(k).Vout_ac_rms_V, rows(k).Dominant_ripple_Hz, ...
        rows(k).Iin_mean_A, rows(k).Pin_mean_W, rows(k).Pout_mean_W, rows(k).Wall_s);
end

results = struct2table(rows);
outCsv = fullfile(outDir, mdl + "_filter_candidate_screen.csv");
writetable(results, outCsv);
fprintf('Wrote %s\n', outCsv);
close_system(char(mdl), 0);
end

function cases = build_cases()
cases = [
    make_case("baseline",             2.2, 2.5, 4700, 10.0, 110, 0.8, 2.20, 12.0)
    make_case("C6800_ESR8",           2.2, 1.2, 6800,  8.0,  44, 2.0, 0.05,  2.0)
    make_case("C6800_ESR12",          2.2, 1.2, 6800, 12.0,  44, 2.0, 0.05,  2.0)
    make_case("C6800_ESR15",          2.2, 1.2, 6800, 15.0,  44, 2.0, 0.05,  2.0)
    make_case("C8200_ESR10",          2.2, 1.2, 8200, 10.0,  44, 2.0, 0.05,  2.0)
    make_case("C8200_ESR15",          2.2, 1.2, 8200, 15.0,  44, 2.0, 0.05,  2.0)
    make_case("C10000_ESR10",         2.2, 1.2,10000, 10.0,  44, 2.0, 0.05,  2.0)
    make_case("C10000_ESR15",         2.2, 1.2,10000, 15.0,  44, 2.0, 0.05,  2.0)
    ];
end

function c = make_case(name, lout_uH, lout_mOhm, bulk_uF, bulk_mOhm, ...
        hf_uF, hf_mOhm, emiEach_uH, emiEach_mOhm)
c = struct('name', string(name), 'lout_uH', lout_uH, ...
    'lout_mOhm', lout_mOhm, 'bulk_uF', bulk_uF, ...
    'bulk_mOhm', bulk_mOhm, 'hf_uF', hf_uF, 'hf_mOhm', hf_mOhm, ...
    'emiEach_uH', emiEach_uH, 'emiEach_mOhm', emiEach_mOhm);
end

function r = run_candidate(mdl, paths, c)
in = Simulink.SimulationInput(char(mdl));
in = in.setModelParameter('StopTime', '0.010', ...
    'ReturnWorkspaceOutputs', 'on', 'SaveFormat', 'Dataset');
in = in.setBlockParameter(paths.vin, 'dc_voltage', '24');
in = in.setBlockParameter(char(mdl + "/MCU_Vout_Setpoint_V"), 'Value', '48');
in = in.setBlockParameter(paths.rload, 'R', '23.04');
in = set_inductor(in, paths.lout, c.lout_uH, c.lout_mOhm);
in = set_capacitor(in, paths.bulk, c.bulk_uF, c.bulk_mOhm);
in = set_capacitor(in, paths.hf, c.hf_uF, c.hf_mOhm);
in = set_inductor(in, paths.emiPos, c.emiEach_uH, c.emiEach_mOhm);
in = set_inductor(in, paths.emiNeg, c.emiEach_uH, c.emiEach_mOhm);

tic;
out = sim(in);
wall = toc;

[tVout, vout] = get_signal(out, mdl, 'vout');
[tIin, iin] = get_signal(out, mdl, 'iin');
[tPin, pin] = get_signal(out, mdl, 'pin');
[tPout, pout] = get_signal(out, mdl, 'pout');
[tPhase, phase] = get_signal(out, mdl, 'phase_us');
tailStart = 0.005;
idxV = tVout >= tailStart;
vTail = vout(idxV);

r = empty_result();
r.Candidate = c.name;
r.Lout_uH = c.lout_uH;
r.Lout_DCR_mOhm = c.lout_mOhm;
r.Cbulk_uF = c.bulk_uF;
r.Cbulk_ESR_mOhm = c.bulk_mOhm;
r.Chf_uF = c.hf_uF;
r.Chf_ESR_mOhm = c.hf_mOhm;
r.Output_EMI_each_uH = c.emiEach_uH;
r.Output_EMI_each_DCR_mOhm = c.emiEach_mOhm;
r.Vout_mean_V = mean(vTail);
r.Vout_error_V = r.Vout_mean_V - 48;
r.Vout_min_V = min(vTail);
r.Vout_max_V = max(vTail);
r.Vout_p2p_V = r.Vout_max_V - r.Vout_min_V;
r.Vout_ac_rms_V = rms(vTail - r.Vout_mean_V);
r.Dominant_ripple_Hz = dominant_frequency(tVout(idxV), vTail);
r.Iin_mean_A = mean(iin(tIin >= tailStart));
r.Pin_mean_W = mean(pin(tPin >= tailStart));
r.Pout_mean_W = mean(pout(tPout >= tailStart));
r.Phase_mean_us = mean(phase(tPhase >= tailStart));
r.Wall_s = wall;
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

function fdom = dominant_frequency(t, y)
dt = 1e-6;
tu = (t(1):dt:t(end)).';
yu = interp1(t, y, tu, 'linear');
yu = yu - mean(yu);
n = numel(yu);
window = 0.5 - 0.5*cos(2*pi*(0:n-1).'/(n-1));
spec = abs(fft(yu .* window));
freq = (0:n-1).'/(n*dt);
valid = freq >= 500 & freq <= 100e3;
[~, idx] = max(spec(valid));
fValid = freq(valid);
fdom = fValid(idx);
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
r = struct('Candidate', "", 'Lout_uH', NaN, 'Lout_DCR_mOhm', NaN, ...
    'Cbulk_uF', NaN, 'Cbulk_ESR_mOhm', NaN, 'Chf_uF', NaN, ...
    'Chf_ESR_mOhm', NaN, 'Output_EMI_each_uH', NaN, ...
    'Output_EMI_each_DCR_mOhm', NaN, 'Vout_mean_V', NaN, ...
    'Vout_error_V', NaN, 'Vout_min_V', NaN, 'Vout_max_V', NaN, ...
    'Vout_p2p_V', NaN, 'Vout_ac_rms_V', NaN, ...
    'Dominant_ripple_Hz', NaN, 'Iin_mean_A', NaN, 'Pin_mean_W', NaN, ...
    'Pout_mean_W', NaN, 'Phase_mean_us', NaN, 'Wall_s', NaN);
end
