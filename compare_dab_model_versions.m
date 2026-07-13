function results = compare_dab_model_versions()
%COMPARE_DAB_MODEL_VERSIONS Run the two H1.7 models under one 300 W test.
%   Both models use 24 V input, a 48 V precharged output, a 7.68 ohm load,
%   RUN asserted, and their saved controller. The result is evidence for
%   version selection; it is not a cold-start qualification.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
otherRoot = 'C:\Users\26488\Desktop\Electric Simulation _02\Electric Simulation';
cases = {
    'CURRENT_H1P7_PHYSICAL', fullfile(notesDir, ...
        'DAB_RA8P1_Final_Physical_MOSFET.slx');
    'OTHER_H1P7_EPS', fullfile(otherRoot, 'DAB_RA8P1_PCB_Notes', ...
        'DAB_RA8P1_Final_Physical_MOSFET.slx')
    };

rows = repmat(empty_row(), size(cases, 1), 1);
for k = 1:size(cases, 1)
    rows(k) = run_case(string(cases{k, 1}), string(cases{k, 2}));
end
results = struct2table(rows);
disp(results);

outDir = fullfile(notesDir, 'final_physical_models_screenshots');
if ~exist(outDir, 'dir')
    mkdir(outDir);
end
writetable(results, fullfile(outDir, ...
    'DAB_RA8P1_H1p7_Version_Baseline_Comparison.csv'));
end

function row = run_case(tag, modelFile)
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
load_system(modelFile);
cleanup = onCleanup(@() close_if_loaded(mdl));

in = Simulink.SimulationInput(mdl);
in = in.setModelParameter('StopTime', '0.0015', 'MaxStep', '1e-8', ...
    'RelTol', '5e-4', 'ReturnWorkspaceOutputs', 'on');
in = set_named(in, mdl, 'VIN_18_32V_Bench_Source', ...
    'dc_voltage', '24');
in = set_named(in, mdl, 'Rload_DC_Output', 'R', ...
    sprintf('%.12g', 48^2/300));
in = set_named(in, mdl, 'RUN_REQ_Model', 'Value', '1');
in = set_named(in, mdl, 'RAW_FAULT_N_Model', 'Value', '1');
if has_block(mdl, 'EPS_TEST_ENABLE_Model')
    in = set_named(in, mdl, 'EPS_TEST_ENABLE_Model', 'Value', '0');
end

for name = ["CSEC_DC_Link_Cap","CSEC_HF_MLCC_Bank", ...
        "COUT_DC_Bulk","COUT_HF_MLCC_Cap"]
    block = find_named(mdl, name);
    in = in.setBlockParameter(block, 'vc_specify', 'on');
    in = in.setBlockParameter(block, 'vc_priority', 'High');
    in = in.setBlockParameter(block, 'vc', '48');
    in = in.setBlockParameter(block, 'vc_unit', 'V');
end

started = tic;
out = sim(in);
wall = toc(started);
vout = get_signal(out, mdl + "_vout");
pin = get_signal(out, mdl + "_pin");
pout = get_signal(out, mdl + "_pout");
phase = get_signal(out, mdl + "_phase_us");
ipri = get_first_signal(out, mdl + ["_ipri_hf","_ipri"]);
isec = get_first_signal(out, mdl + ["_isec_hf","_isec"]);

tailStart = 0.0012;
vMask = vout.time >= tailStart;
phaseMask = phase.time >= tailStart;
pinMask = pin.time >= tailStart;
poutMask = pout.time >= tailStart;
currentStart = 0.0002;
vFit = polyfit(vout.time(vMask), vout.values(vMask), 1);
row = empty_row();
row.Version = tag;
row.ModelBytes = dir(modelFile).bytes;
row.VoutMean_V = mean(vout.values(vMask));
row.VoutError_mV = 1000*(row.VoutMean_V - 48);
row.VoutP2P_V = range(vout.values(vMask));
row.VoutSlope_Vps = vFit(1);
row.PinMean_W = mean(pin.values(pinMask));
row.PoutMean_W = mean(pout.values(poutMask));
row.IpriPeak_A = max(abs(ipri.values(ipri.time >= currentStart)));
row.IsecPeak_A = max(abs(isec.values(isec.time >= currentStart)));
row.PhaseMean_us = mean(phase.values(phaseMask));
row.PhaseMin_us = min(phase.values(phaseMask));
row.PhaseMax_us = max(phase.values(phaseMask));
row.HasMosPackageParasitics = mos_package_parasitics_present(mdl);
row.HasPhysicalTmcsConductor = has_block(mdl, 'R_TMCS_PRI_0p7mOhm') && ...
    has_block(mdl, 'R_TMCS_SEC_0p7mOhm');
row.HasTmcsDynamicProtection = has_block(mdl, 'TMCS_IPRI_1p1MHz_LPF') && ...
    has_block(mdl, 'RAW_FAULT_N_With_OC');
row.Wall_s = wall;
fprintf('%s: Vmean=%.6f V, Vpp=%.6f V, Pin=%.2f W, Ipk=%.2f/%.2f A\n', ...
    tag, row.VoutMean_V, row.VoutP2P_V, row.PinMean_W, ...
    row.IpriPeak_A, row.IsecPeak_A);
end

function in = set_named(in, mdl, name, parameter, value)
in = in.setBlockParameter(find_named(mdl, name), parameter, value);
end

function tf = has_block(mdl, name)
tf = ~isempty(find_system(mdl, 'SearchDepth', 1, 'Name', char(name)));
end

function path = find_named(mdl, name)
matches = find_system(mdl, 'LookUnderMasks', 'all', 'FollowLinks', 'on', ...
    'SearchDepth', inf, 'Name', char(name));
assert(~isempty(matches), 'Block %s was not found.', name);
path = matches{1};
end

function signal = get_first_signal(out, names)
available = string(out.who);
for name = names
    if any(available == name)
        signal = get_signal(out, name);
        return;
    end
end
error('None of the requested current signals exists.');
end

function signal = get_signal(out, name)
data = out.get(name);
signal.time = data.time(:);
signal.values = data.signals.values(:);
end

function tf = mos_package_parasitics_present(mdl)
blocks = find_system(mdl, 'LookUnderMasks', 'all', 'FollowLinks', 'on', ...
    'RegExp', 'on', 'Name', '^Q[1-8]_.*_(HS|LS)$');
tf = numel(blocks) == 8;
for k = 1:numel(blocks)
    try
        tf = tf && str2double(get_param(blocks{k}, 'LS')) > 0 && ...
            str2double(get_param(blocks{k}, 'LD')) > 0 && ...
            str2double(get_param(blocks{k}, 'LG')) > 0;
    catch
        tf = false;
    end
end
end

function close_if_loaded(mdl)
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
end

function row = empty_row()
row = struct('Version', "", 'ModelBytes', NaN, 'VoutMean_V', NaN, ...
    'VoutError_mV', NaN, 'VoutP2P_V', NaN, 'VoutSlope_Vps', NaN, ...
    'PinMean_W', NaN, 'PoutMean_W', NaN, 'IpriPeak_A', NaN, ...
    'IsecPeak_A', NaN, 'PhaseMean_us', NaN, 'PhaseMin_us', NaN, ...
    'PhaseMax_us', NaN, 'HasMosPackageParasitics', false, ...
    'HasPhysicalTmcsConductor', false, ...
    'HasTmcsDynamicProtection', false, 'Wall_s', NaN);
end
