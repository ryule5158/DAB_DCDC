function results = optimize_output_ripple_circuit(mode, vAlpha, trimScale, phaseCal)
%OPTIMIZE_OUTPUT_RIPPLE_CIRCUIT Sweep real output-filter values for ripple.
%
% mode:
%   "screen" - run worst-case subset for several circuit candidates.
%   "cal"    - run worst-case subset for the selected optimized circuit.
%   "final"  - run all final cases for the selected optimized circuit.

if nargin < 1
    mode = "screen";
end
if nargin < 2
    vAlpha = NaN;
end
if nargin < 3
    trimScale = NaN;
end
if nargin < 4
    phaseCal = false;
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
if ~isnan(vAlpha) || ~isnan(trimScale) || phaseCal
    set_controller_calibration(mdl, vAlpha, trimScale, phaseCal);
end

if mode == "screen"
    cases = [
        24 36 20
        24 36 100
        24 36 500
        24 40 100
        24 44 100
        24 48 100
        24 48 300
        24 48 500
        ];
    candidates = screening_candidates();
elseif mode == "cal"
    cases = [
        24 36 20
        24 36 100
        24 36 500
        24 40 100
        24 44 100
        24 48 100
        24 48 300
        24 48 500
        ];
    candidates = final_candidate();
else
    cases = [
        24 36 20
        24 36 50
        24 36 100
        24 40 20
        24 40 50
        24 40 100
        24 44 20
        24 44 50
        24 44 100
        24 48 20
        24 48 50
        24 48 100
        24 36 300
        24 36 500
        24 48 300
        24 48 500
        ];
    candidates = final_candidate();
end

rows = repmat(empty_result(), numel(candidates) * size(cases, 1), 1);
rowIdx = 0;
for c = 1:numel(candidates)
    for k = 1:size(cases, 1)
        cfg.Vin = cases(k, 1);
        cfg.Vref = cases(k, 2);
        cfg.Ptarget = cases(k, 3);
        rowIdx = rowIdx + 1;
        rows(rowIdx) = run_one_case(mdl, candidates(c), cfg);
        fprintf(['%s Vin=%g Vref=%g P=%g Vmean=%.4f Verr=%.4f Vpp=%.4f ' ...
            'passV=%d passRipple=%d phase=%.5f us wall=%.1f s\n'], ...
            rows(rowIdx).Candidate, rows(rowIdx).Vin_set_V, rows(rowIdx).Vref_V, ...
            rows(rowIdx).Ptarget_W, rows(rowIdx).Vout_mean_V, rows(rowIdx).Vout_error_V, ...
            rows(rowIdx).Vout_p2p_V, rows(rowIdx).Voltage_pass_0p1, ...
            rows(rowIdx).Ripple_pass_0p25, rows(rowIdx).Phase_mean_us, rows(rowIdx).Wall_s);
    end
end

results = struct2table(rows);
outCsv = fullfile(outDir, mdl + "_output_ripple_" + mode + ".csv");
writetable(results, outCsv);
fprintf('Wrote %s\n', outCsv);
end

function candidates = screening_candidates()
candidates = [
    candidate("baseline", 4.7, 0.0034, 47, 0.036, 470, 0.0036, 22, 0.0020, 2.2, 0.012, 1e9, 100, 0.003, 100000)
    candidate("Cout2200_ESR5m_HF110", 4.7, 0.0034, 47, 0.036, 2200, 0.0050, 110, 0.0008, 2.2, 0.012, 1e9, 100, 0.003, 100000)
    candidate("Cout2200_ESR20m_HF110", 4.7, 0.0034, 47, 0.036, 2200, 0.0200, 110, 0.0008, 2.2, 0.012, 1e9, 100, 0.003, 100000)
    candidate("Cout4700_ESR10m_HF110", 4.7, 0.0034, 47, 0.036, 4700, 0.0100, 110, 0.0008, 2.2, 0.012, 1e9, 100, 0.003, 100000)
    ];
end

function candidates = final_candidate()
candidates = candidate("optimized_Cout4700u_ESR10m_HF110u", ...
    4.7, 0.0034, 47, 0.036, 4700, 0.0100, 110, 0.0008, 2.2, 0.012, 1e9, 100, 0.003, 100000);
end

function c = candidate(name, loutUh, loutR, csecUf, csecR, coutUf, coutR, chfUf, chfR, emiUh, emiR, dampR, dampUf, dampCapR, bleederR)
c = struct( ...
    'Name', string(name), ...
    'Lout_uH', loutUh, ...
    'Lout_R_Ohm', loutR, ...
    'Csec_uF', csecUf, ...
    'Csec_R_Ohm', csecR, ...
    'Cout_uF', coutUf, ...
    'Cout_R_Ohm', coutR, ...
    'Chf_uF', chfUf, ...
    'Chf_R_Ohm', chfR, ...
    'Emi_uH', emiUh, ...
    'Emi_R_Ohm', emiR, ...
    'Damp_R_Ohm', dampR, ...
    'Damp_uF', dampUf, ...
    'Damp_Cap_R_Ohm', dampCapR, ...
    'Bleeder_R_Ohm', bleederR);
end

function r = run_one_case(mdl, cand, cfg)
rload = cfg.Vref^2 / cfg.Ptarget;
in = Simulink.SimulationInput(char(mdl));
in = in.setModelParameter('StopTime', '0.00045');
in = in.setModelParameter('ReturnWorkspaceOutputs', 'on');
in = in.setModelParameter('SaveFormat', 'Dataset');
in = in.setBlockParameter(resolve_block_path(mdl, "VIN_18_32V_Bench_Source"), ...
    'dc_voltage', sprintf('%.10g', cfg.Vin));
in = in.setBlockParameter(char(mdl + "/MCU_Vout_Setpoint_V"), ...
    'Value', sprintf('%.10g', cfg.Vref));
in = in.setBlockParameter(resolve_block_path(mdl, "Rload_DC_Output"), ...
    'R', sprintf('%.10g', rload));

in = configure_candidate(in, mdl, cand, cfg.Vref);

tic;
out = sim(in);
wall = toc;

[tVin, vin] = get_signal(out, mdl, 'vin');
[tIin, iin] = get_signal(out, mdl, 'iin');
[tVout, vout] = get_signal(out, mdl, 'vout');
[tIout, iout] = get_signal(out, mdl, 'iout');
[tPin, pin] = get_signal(out, mdl, 'pin');
[tPout, pout] = get_signal(out, mdl, 'pout');
[tEff, eff] = get_signal(out, mdl, 'efficiency');
[tPh, ph] = get_signal(out, mdl, 'phase_us');

idxV = tail_index(tVout);
r = empty_result();
r.Candidate = char(cand.Name);
r.Vin_set_V = cfg.Vin;
r.Vref_V = cfg.Vref;
r.Ptarget_W = cfg.Ptarget;
r.Rload_Ohm = rload;
r.Lout_uH = cand.Lout_uH;
r.Csec_uF = cand.Csec_uF;
r.Cout_uF = cand.Cout_uF;
r.Chf_uF = cand.Chf_uF;
r.Vin_mean_V = mean(vin(tail_index(tVin)));
r.Vout_mean_V = mean(vout(idxV));
r.Vout_error_V = r.Vout_mean_V - cfg.Vref;
r.Voltage_pass_0p1 = abs(r.Vout_error_V) <= 0.1;
r.Vout_final_V = vout(end);
r.Vout_p2p_V = max(vout(idxV)) - min(vout(idxV));
r.Ripple_pass_0p25 = r.Vout_p2p_V <= 0.25;
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

function in = configure_candidate(in, mdl, cand, vref)
in = configure_inductor(in, resolve_block_path(mdl, "LOUT_Filter_Inductor"), ...
    cand.Lout_uH, cand.Lout_R_Ohm);
in = configure_inductor(in, resolve_block_path(mdl, "LOUT_EMI_Pos_Choke"), ...
    cand.Emi_uH, cand.Emi_R_Ohm);
in = configure_inductor(in, resolve_block_path(mdl, "LOUT_EMI_Return_Choke"), ...
    cand.Emi_uH, cand.Emi_R_Ohm);
in = configure_cap(in, mdl, "CSEC_DC_Link_Cap", cand.Csec_uF, cand.Csec_R_Ohm, vref);
in = configure_cap(in, mdl, "COUT_DC_Bulk", cand.Cout_uF, cand.Cout_R_Ohm, vref);
in = configure_cap(in, mdl, "COUT_HF_MLCC_Cap", cand.Chf_uF, cand.Chf_R_Ohm, vref);
in = in.setBlockParameter(resolve_block_path(mdl, "ROUT_Bleeder"), 'R', sprintf('%.10g', cand.Bleeder_R_Ohm));
in = in.setBlockParameter(resolve_block_path(mdl, "ROUT_Bleeder"), 'R_unit', 'Ohm');
if block_exists(mdl, "COUT_Damping_R")
    in = in.setBlockParameter(resolve_block_path(mdl, "COUT_Damping_R"), 'R', sprintf('%.10g', cand.Damp_R_Ohm));
    in = in.setBlockParameter(resolve_block_path(mdl, "COUT_Damping_R"), 'R_unit', 'Ohm');
end
if block_exists(mdl, "COUT_Damping_Cap")
    in = configure_cap(in, mdl, "COUT_Damping_Cap", cand.Damp_uF, cand.Damp_Cap_R_Ohm, vref);
end
end

function in = configure_cap(in, mdl, blockName, capUf, esr, vref)
blk = resolve_block_path(mdl, blockName);
in = in.setBlockParameter(blk, 'c', sprintf('%.10g', capUf));
in = in.setBlockParameter(blk, 'c_unit', 'uF');
in = in.setBlockParameter(blk, 'r', sprintf('%.10g', esr));
in = in.setBlockParameter(blk, 'r_unit', 'Ohm');
in = in.setBlockParameter(blk, 'vc_specify', 'on');
in = in.setBlockParameter(blk, 'vc_priority', 'High');
in = in.setBlockParameter(blk, 'vc', sprintf('%.10g', vref));
in = in.setBlockParameter(blk, 'vc_unit', 'V');
end

function in = configure_inductor(in, blk, inductanceUh, resistanceOhm)
in = in.setBlockParameter(blk, 'l', sprintf('%.10g', inductanceUh));
in = in.setBlockParameter(blk, 'l_unit', 'uH');
in = in.setBlockParameter(blk, 'r', sprintf('%.10g', resistanceOhm));
in = in.setBlockParameter(blk, 'r_unit', 'Ohm');
end

function blk = resolve_block_path(mdl, blockName)
matches = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, 'Name', char(blockName));
if isempty(matches)
    error('Block "%s" was not found in model "%s".', blockName, mdl);
end
blk = matches{1};
end

function exists = block_exists(mdl, blockName)
matches = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, 'Name', char(blockName));
exists = ~isempty(matches);
end

function set_controller_calibration(mdl, alpha, trimScale, phaseCal)
rt = sfroot;
chart = rt.find('-isa', 'Stateflow.EMChart', ...
    'Path', char(mdl + "/RA8P1_Realtime_Voltage_PI"));
if isempty(chart)
    error('RA8P1_Realtime_Voltage_PI chart was not found.');
end
script = string(chart(1).Script);
if ~isnan(alpha)
    oldLine = "vFilt = vFilt + 0.20*(Vout - vFilt);";
    newLine = "vFilt = vFilt + " + sprintf('%.4g', alpha) + "*(Vout - vFilt);";
    if ~contains(script, oldLine)
        error('Expected vFilt alpha line was not found.');
    end
    script = replace(script, oldLine, newLine);
    fprintf('Temporary controller vFilt alpha = %.4g\n', alpha);
end
if ~isnan(trimScale)
    oldLine = "trim = 0.982*trim + 0.0017e-6*err + 0.00055e-6*dErr;";
    kpTrim = 0.0017 * trimScale;
    kdTrim = 0.00055 * trimScale;
    newLine = "trim = 0.982*trim + " + sprintf('%.6g', kpTrim) + "e-6*err + " + sprintf('%.6g', kdTrim) + "e-6*dErr;";
    if ~contains(script, oldLine)
        error('Expected trim update line was not found.');
    end
    script = replace(script, oldLine, newLine);
    fprintf('Temporary controller trim scale = %.4g\n', trimScale);
end
if phaseCal
    oldLine = "phase = min(max(phaseUs*1e-6, -0.55e-6), 0.46e-6);";
    calLines = [
        "if buckDelta > 0.6"
        "    phaseUs = phaseUs + 0.095*exp(-((Iout - 13.0)/3.0)^2)*exp(-((buckDelta - 6.0)/1.4)^2);"
        "else"
        "    phaseUs = phaseUs + 0.030*(1 - exp(-max(Iout - 7.0, 0)/2.0));"
        "end"
        oldLine
        ];
    newLine = strjoin(calLines, newline);
    if ~contains(script, oldLine)
        error('Expected base_phase output clamp line was not found.');
    end
    script = replace(script, oldLine, newLine);
    fprintf('Temporary output-filter phase calibration enabled\n');
end
chart(1).Script = char(script);
end

function r = empty_result()
r = struct('Candidate', "", 'Vin_set_V', NaN, 'Vref_V', NaN, 'Ptarget_W', NaN, ...
    'Rload_Ohm', NaN, 'Lout_uH', NaN, 'Csec_uF', NaN, 'Cout_uF', NaN, ...
    'Chf_uF', NaN, 'Vin_mean_V', NaN, 'Vout_mean_V', NaN, ...
    'Vout_error_V', NaN, 'Voltage_pass_0p1', false, 'Vout_final_V', NaN, ...
    'Vout_p2p_V', NaN, 'Ripple_pass_0p25', false, 'Iin_mean_A', NaN, ...
    'Iout_mean_A', NaN, 'Iload_calc_A', NaN, 'Pin_mean_W', NaN, ...
    'Pout_mean_W', NaN, 'Pload_calc_W', NaN, 'Eff_mean_pct', NaN, ...
    'Phase_mean_us', NaN, 'Wall_s', NaN);
end

function idx = tail_index(t)
n = numel(t);
idx = max(1, floor(n*0.75)):n;
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
