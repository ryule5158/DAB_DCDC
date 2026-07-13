function results = sweep_current_physical_eps_candidates()
%SWEEP_CURRENT_PHYSICAL_EPS_CANDIDATES Tune EPS on the physical H1.7 model.
%   The saved model is never modified. Each candidate is injected into the
%   controller in memory, simulated from a 48 V precharge at 300 W, and the
%   original chart script is restored before the model is closed.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
modelFile = fullfile(notesDir, mdl + ".slx");
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
load_system(modelFile);
modelCleanup = onCleanup(@() close_if_loaded(mdl));
chart = find_chart(mdl);
originalScript = string(chart.Script);
scriptCleanup = onCleanup(@() restore_script(chart, originalScript));

innerGrid = [0.16 0.20 0.24];
externalGrid = [0.24 0.27 0.30 0.33 0.36];
nCases = numel(innerGrid)*numel(externalGrid);
rows = repmat(empty_row(), nCases, 1);
index = 0;
for innerUs = innerGrid
    for externalUs = externalGrid
        index = index + 1;
        chart.Script = char(eps_script(originalScript, innerUs, externalUs));
        set_param(mdl, 'SimulationCommand', 'update');
        out = sim(make_input(mdl));
        rows(index) = metrics(out, mdl, innerUs, externalUs);
        fprintf('EPS inner %.2f us, phase %.2f us: transfer %.2f W, Ipk %.2f/%.2f A\n', ...
            innerUs, externalUs, rows(index).TransferEstimate_W, ...
            rows(index).IpriPeak_A, rows(index).IsecPeak_A);
    end
end

results = struct2table(rows);
results.DistanceTo300_W = abs(results.TransferEstimate_W - 300);
results = sortrows(results, {'SafeCurrent','DistanceTo300_W'}, ...
    {'descend','ascend'});
disp(results);
outDir = fullfile(notesDir, 'final_physical_models_screenshots');
writetable(results, fullfile(outDir, ...
    'DAB_RA8P1_H1p7_CurrentPhysical_EPS_Sweep.csv'));
end

function in = make_input(mdl)
in = Simulink.SimulationInput(mdl);
in = in.setModelParameter('StopTime', '0.0006', 'MaxStep', '1e-8', ...
    'RelTol', '5e-4', 'ReturnWorkspaceOutputs', 'on');
in = set_named(in, mdl, 'VIN_18_32V_Bench_Source', 'dc_voltage', '24');
in = set_named(in, mdl, 'Rload_DC_Output', 'R', ...
    sprintf('%.12g', 48^2/300));
in = set_named(in, mdl, 'RUN_REQ_Model', 'Value', '1');
in = set_named(in, mdl, 'RAW_FAULT_N_Model', 'Value', '1');
for name = ["CSEC_DC_Link_Cap","CSEC_HF_MLCC_Bank", ...
        "COUT_DC_Bulk","COUT_HF_MLCC_Cap"]
    block = find_named(mdl, name);
    in = in.setBlockParameter(block, 'vc_specify', 'on');
    in = in.setBlockParameter(block, 'vc_priority', 'High');
    in = in.setBlockParameter(block, 'vc', '48');
    in = in.setBlockParameter(block, 'vc_unit', 'V');
end
end

function row = metrics(out, mdl, innerUs, externalUs)
pin = out.get(mdl + "_pin");
vout = out.get(mdl + "_vout");
ipri = out.get(mdl + "_ipri_hf");
isec = out.get(mdl + "_isec_hf");
phase = out.get(mdl + "_phase_us");
tail = 0.00045;
vMask = vout.time >= tail;
pinMask = pin.time >= tail;
coefficients = polyfit(vout.time(vMask), vout.signals.values(vMask), 1);
vMean = mean(vout.signals.values(vMask));
transfer = 300 + 0.008*vMean*coefficients(1);
priPeak = max(abs(ipri.signals.values(ipri.time >= 0.0002)));
secPeak = max(abs(isec.signals.values(isec.time >= 0.0002)));
phaseMask = phase.time >= tail;
row = empty_row();
row.Inner_us = innerUs;
row.External_us = externalUs;
row.PinMean_W = mean(pin.signals.values(pinMask));
row.VoutMean_V = vMean;
row.VoutP2P_V = range(vout.signals.values(vMask));
row.VoutSlope_Vps = coefficients(1);
row.TransferEstimate_W = transfer;
row.IpriPeak_A = priPeak;
row.IsecPeak_A = secPeak;
row.PhaseMean_us = mean(phase.signals.values(phaseMask));
% Minimum tolerance thresholds from the frozen hardware audit.
row.SafeCurrent = priPeak < 58.752 && secPeak < 35.984;
end

function script = eps_script(originalScript, innerUs, externalUs)
needle = [
    "if normalEnabled && faultOk"
    "    phase = normalRamp*(base_phase(Vref, vinFilt, iFilt) + trim);"
    "    phase = min(max(phase, -0.55e-6), 0.46e-6);"
    "    [a,an] = halfbridge(t,T,deadTime);"
    "    [b,bn] = halfbridge(t+T/2,T,deadTime);"
    "    [c,cn] = halfbridge(t-phase,T,deadTime);"
    "    [d,dn] = halfbridge(t-phase+T/2,T,deadTime);"
    "    g1 = 10*a;  g2 = 10*an;"
    "    g3 = 10*b;  g4 = 10*bn;"
    "    g5 = 10*c;  g6 = 10*cn;"
    "    g7 = 10*d;  g8 = 10*dn;"
    "    PhaseUs = phase*1e6;"
    ];
replacement = [
    "if normalEnabled && faultOk"
    sprintf("    inner = %.12ge-6;", innerUs)
    sprintf("    phase = %.12ge-6;", externalUs)
    "    [a,an] = halfbridge(t,T,deadTime);"
    "    [b,bn] = halfbridge(t+T/2-inner,T,deadTime);"
    "    [c,cn] = halfbridge(t-phase,T,deadTime);"
    "    [d,dn] = halfbridge(t-phase+T/2,T,deadTime);"
    "    g1 = 10*a;  g2 = 10*an;"
    "    g3 = 10*b;  g4 = 10*bn;"
    "    g5 = 10*c;  g6 = 10*cn;"
    "    g7 = 10*d;  g8 = 10*dn;"
    "    PhaseUs = phase*1e6;"
    ];
needle = strjoin(needle, newline);
replacement = strjoin(replacement, newline);
assert(count(originalScript, needle) == 1, ...
    'The expected NORMAL_SPS output branch was not found once.');
script = replace(originalScript, needle, replacement);
end

function chart = find_chart(mdl)
rt = sfroot;
chart = rt.find('-isa', 'Stateflow.EMChart', ...
    'Path', char(mdl + "/RA8P1_Realtime_Voltage_PI"));
assert(~isempty(chart), 'Controller chart was not found.');
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

function restore_script(chart, script)
if isvalid(chart)
    chart.Script = char(script);
end
end

function close_if_loaded(mdl)
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
end

function row = empty_row()
row = struct('Inner_us', NaN, 'External_us', NaN, 'PinMean_W', NaN, ...
    'VoutMean_V', NaN, 'VoutP2P_V', NaN, 'VoutSlope_Vps', NaN, ...
    'TransferEstimate_W', NaN, 'IpriPeak_A', NaN, 'IsecPeak_A', NaN, ...
    'PhaseMean_us', NaN, 'SafeCurrent', false);
end
