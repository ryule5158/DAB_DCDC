function results = validate_h1p7_final_repeatability_and_leakage()
%VALIDATE_H1P7_FINAL_REPEATABILITY_AND_LEAKAGE Three repeats plus Llk corners.
%   All cases use the final physical model, 24 V input, 300 W load, 48 V
%   precharge, minimum-tolerance OC thresholds, and a 28-30 ms tail window.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
load_system(fullfile(notesDir, mdl + ".slx"));
cleanup = onCleanup(@() close_if_loaded(mdl));

labels = ["NOMINAL_REPEAT_1";"NOMINAL_REPEAT_2";"NOMINAL_REPEAT_3"; ...
    "LLK_MIN_MINUS20PCT";"LLK_MAX_PLUS20PCT"];
leakage = [0.5;0.5;0.5;0.4;0.6]*1e-6;
rows = repmat(empty_row(), numel(labels), 1);
for k = 1:numel(labels)
    out = sim(make_input(mdl, leakage(k)));
    rows(k) = metrics(out, mdl, labels(k), leakage(k));
    fprintf('%s: Vmean %.9f V, Vpp %.6f V, Ipk %.3f/%.3f A, pass %d\n', ...
        labels(k), rows(k).VoutMean_V, rows(k).VoutP2P_V, ...
        rows(k).IpriPeak_A, rows(k).IsecPeak_A, rows(k).Pass);
end
results = struct2table(rows);
nominal = results(1:3,:);
results.RepeatVmeanSpread_uV = zeros(height(results),1);
results.RepeatVppSpread_uV = zeros(height(results),1);
results.RepeatVmeanSpread_uV(1:3) = ...
    1e6*(max(nominal.VoutMean_V)-min(nominal.VoutMean_V));
results.RepeatVppSpread_uV(1:3) = ...
    1e6*(max(nominal.VoutP2P_V)-min(nominal.VoutP2P_V));
disp(results);

outDir = fullfile(notesDir, 'final_physical_models_screenshots');
writetable(results, fullfile(outDir, ...
    'DAB_RA8P1_H1p7_Final_ThreeRun_Leakage_Corners.csv'));
assert(all(results.Pass), 'Final repeatability or leakage-corner case failed.');
assert(results.RepeatVmeanSpread_uV(1) <= 1, ...
    'Three nominal runs were not deterministic within 1 uV.');
fprintf('PASS: three nominal repeats and both leakage corners.\n');
end

function in = make_input(mdl, leakageH)
in = Simulink.SimulationInput(mdl);
in = in.setModelParameter('StopTime', '0.030', 'MaxStep', '1e-8', ...
    'RelTol', '5e-4', 'ReturnWorkspaceOutputs', 'on');
in = set_named(in, mdl, 'VIN_18_32V_Bench_Source', 'dc_voltage', '24');
in = set_named(in, mdl, 'Rload_DC_Output', 'R', ...
    sprintf('%.12g', 48^2/300));
in = set_named(in, mdl, 'RUN_REQ_Model', 'Value', '1');
in = set_named(in, mdl, 'RAW_FAULT_N_Model', 'Value', '1');
in = set_named(in, mdl, 'IPRI_HW_OC_Threshold_A', 'Value', '58.752');
in = set_named(in, mdl, 'ISEC_HW_OC_Threshold_A', 'Value', '35.984');
% The Simscape block stores this compile-time value in uH, parameter "l".
in = set_named(in, mdl, 'Llk_Secondary_Transfer', 'l', ...
    sprintf('%.12g', leakageH*1e6));
for name = ["CSEC_DC_Link_Cap","CSEC_HF_MLCC_Bank", ...
        "COUT_DC_Bulk","COUT_HF_MLCC_Cap"]
    block = find_named(mdl, name);
    in = in.setBlockParameter(block, 'vc_specify', 'on');
    in = in.setBlockParameter(block, 'vc_priority', 'High');
    in = in.setBlockParameter(block, 'vc', '48');
    in = in.setBlockParameter(block, 'vc_unit', 'V');
end
end

function row = metrics(out, mdl, label, leakageH)
vout = signal(out, mdl + "_vout");
ipri = signal(out, mdl + "_ipri_hf");
isec = signal(out, mdl + "_isec_hf");
phase = signal(out, mdl + "_phase_us");
rawFault = signal(out, mdl + "_raw_fault_n");
latch = signal(out, mdl + "_fault_latch_n");
tail = vout.time >= 0.028;
coefficients = polyfit(vout.time(tail), vout.values(tail), 1);
vMean = mean(vout.values(tail));
vP2P = max(vout.values(tail))-min(vout.values(tail));
currentStart = 0.0002;
row = empty_row();
row.Case = label;
row.LlkSecondary_uH = leakageH*1e6;
row.VoutMean_V = vMean;
row.VoutError_mV = 1000*(vMean-48);
row.VoutP2P_V = vP2P;
row.VoutSlope_Vps = coefficients(1);
row.IpriPeak_A = max(abs(ipri.values(ipri.time >= currentStart)));
row.IsecPeak_A = max(abs(isec.values(isec.time >= currentStart)));
phaseTail = phase.values(phase.time >= 0.028);
row.PhaseMean_us = mean(phaseTail);
row.RawFaultNMin = min(rawFault.values);
row.FaultLatchNMin = min(latch.values);
errorLimit = 10;
if startsWith(label, "NOMINAL")
    errorLimit = 1;
end
row.Pass = abs(row.VoutError_mV) <= errorLimit && ...
    row.VoutP2P_V < 0.1 && abs(row.VoutSlope_Vps) <= 1 && ...
    row.IpriPeak_A < 58.752 && row.IsecPeak_A < 35.984 && ...
    row.RawFaultNMin > 0.5 && row.FaultLatchNMin > 0.5;
end

function in = set_named(in, mdl, name, parameter, value)
in = in.setBlockParameter(find_named(mdl, name), parameter, value);
end

function path = find_named(mdl, name)
matches = find_system(mdl, 'LookUnderMasks', 'all', 'FollowLinks', 'on', ...
    'SearchDepth', inf, 'Name', char(name));
assert(~isempty(matches), 'Block %s was not found.', name);
path = matches{1};
end

function s = signal(out, name)
data = out.get(name);
s.time = data.time(:);
s.values = data.signals.values(:);
end

function row = empty_row()
row = struct('Case', "", 'LlkSecondary_uH', NaN, ...
    'VoutMean_V', NaN, 'VoutError_mV', NaN, 'VoutP2P_V', NaN, ...
    'VoutSlope_Vps', NaN, 'IpriPeak_A', NaN, 'IsecPeak_A', NaN, ...
    'PhaseMean_us', NaN, 'RawFaultNMin', NaN, ...
    'FaultLatchNMin', NaN, 'Pass', false);
end

function close_if_loaded(mdl)
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
end
