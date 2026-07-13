function summary = validate_h1p7_merged_physical_model()
%VALIDATE_H1P7_MERGED_PHYSICAL_MODEL Validate merged EPS and protection.
%   Runs a long 300 W regulation case, a transient OC latch case, and a
%   5 ns gate/Vds case. Each case starts from a fresh simulation state.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
outDir = fullfile(notesDir, 'final_physical_models_screenshots');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
load_system(fullfile(notesDir, mdl + ".slx"));
cleanup = onCleanup(@() close_if_loaded(mdl));

nominal = run_nominal(mdl);
fault = run_fault_latch(mdl);
gate = run_gate_case(mdl);
writetable(nominal, fullfile(outDir, ...
    'DAB_RA8P1_H1p7_Merged_300W_Long.csv'));
writetable(fault, fullfile(outDir, ...
    'DAB_RA8P1_H1p7_Merged_Fault_Latch.csv'));
writetable(gate, fullfile(outDir, ...
    'DAB_RA8P1_H1p7_Merged_Gate_Vds.csv'));
summary = struct('Nominal', nominal, 'Fault', fault, 'Gate', gate);

assert(nominal.Pass, 'Merged H1.7 long 300 W regulation failed.');
assert(fault.Pass, 'Merged H1.7 fault latch failed.');
assert(gate.Pass, 'Merged H1.7 gate/Vds validation failed.');
fprintf('PASS: merged H1.7 physical EPS and hardware protection.\n');
end

function result = run_nominal(mdl)
in = base_input(mdl, 300, 0.030, 1e-8, 48);
% Minimum tolerance thresholds verify nuisance-trip margin.
in = set_named(in, mdl, 'IPRI_HW_OC_Threshold_A', 'Value', '58.752');
in = set_named(in, mdl, 'ISEC_HW_OC_Threshold_A', 'Value', '35.984');
out = sim(in);
vout = signal(out, mdl + "_vout");
pin = signal(out, mdl + "_pin");
pout = signal(out, mdl + "_pout");
ipri = signal(out, mdl + "_ipri_hf");
isec = signal(out, mdl + "_isec_hf");
ipriTmcs = signal(out, mdl + "_ipri_tmcs");
isecTmcs = signal(out, mdl + "_isec_tmcs");
phase = signal(out, mdl + "_phase_us");
rawFault = signal(out, mdl + "_raw_fault_n");
latch = signal(out, mdl + "_fault_latch_n");
enable = signal(out, mdl + "_driver_enable");

tailStart = 0.028;
vMask = vout.time >= tailStart;
coefficients = polyfit(vout.time(vMask), vout.values(vMask), 1);
vMean = mean(vout.values(vMask));
vP2P = max(vout.values(vMask))-min(vout.values(vMask));
currentStart = 0.0002;
activeMask = enable.time >= 0.00025;
phaseTail = phase.values(phase.time >= tailStart);
result = table(vMean, 1000*(vMean-48), vP2P, coefficients(1), ...
    mean(pin.values(pin.time >= tailStart)), ...
    mean(pout.values(pout.time >= tailStart)), ...
    max(abs(ipri.values(ipri.time >= currentStart))), ...
    max(abs(isec.values(isec.time >= currentStart))), ...
    max(abs(ipriTmcs.values(ipriTmcs.time >= currentStart))), ...
    max(abs(isecTmcs.values(isecTmcs.time >= currentStart))), ...
    mean(phaseTail), min(phaseTail), max(phaseTail), ...
    min(rawFault.values), min(latch.values), ...
    min(enable.values(activeMask)), false, ...
    'VariableNames', {'VoutMean_V','VoutError_mV','VoutP2P_V', ...
    'VoutSlope_Vps','PinMean_W','PoutMean_W','IpriRawPeak_A', ...
    'IsecRawPeak_A','IpriTmcsPeak_A','IsecTmcsPeak_A', ...
    'PhaseMean_us','PhaseMin_us','PhaseMax_us', ...
    'RawFaultNMin','FaultLatchNMin', ...
    'DriverEnableMinActive','Pass'});
result.Pass = abs(result.VoutError_mV) <= 10 && ...
    result.VoutP2P_V < 0.1 && abs(result.VoutSlope_Vps) <= 1 && ...
    result.IpriRawPeak_A < 58.752 && result.IsecRawPeak_A < 35.984 && ...
    result.RawFaultNMin > 0.5 && result.FaultLatchNMin > 0.5 && ...
    result.DriverEnableMinActive > 0.5;
disp(result);
end

function result = run_fault_latch(mdl)
in = base_input(mdl, 300, 0.001, 1e-8, 48);
in = set_named(in, mdl, 'IPRI_HW_OC_Threshold_A', 'Value', '25');
in = set_named(in, mdl, 'ISEC_HW_OC_Threshold_A', 'Value', '15');
out = sim(in);
rawFault = signal(out, mdl + "_raw_fault_n");
latch = signal(out, mdl + "_fault_latch_n");
enable = signal(out, mdl + "_driver_enable");
phase = signal(out, mdl + "_phase_us");
first = find(rawFault.values < 0.5, 1, 'first');
assert(~isempty(first), 'Injected OC did not trigger.');
faultTime = rawFault.time(first);
released = any(rawFault.values(rawFault.time >= faultTime+50e-6) > 0.5);
postTime = faultTime + 100e-6;
postLatch = max(latch.values(latch.time >= postTime));
postEnable = max(enable.values(enable.time >= postTime));
postPhase = max(abs(phase.values(phase.time >= postTime)));
result = table(faultTime*1e6, released, postLatch, postEnable, ...
    postPhase, false, 'VariableNames', {'FirstRawFault_us', ...
    'RawFaultReleasedLater','MaxPostFaultLatchN', ...
    'MaxPostFaultDriverEnable','MaxPostFaultPhase_us','Pass'});
result.Pass = result.RawFaultReleasedLater && ...
    result.MaxPostFaultLatchN < 0.5 && ...
    result.MaxPostFaultDriverEnable < 0.5 && ...
    result.MaxPostFaultPhase_us <= 1e-6;
disp(result);
end

function result = run_gate_case(mdl)
in = base_input(mdl, 300, 0.0004, 5e-9, 48);
in = in.setModelParameter('RelTol', '5e-4', 'SimscapeLogType', 'all');
in = set_named(in, mdl, 'IPRI_HW_OC_Threshold_A', 'Value', '58.752');
in = set_named(in, mdl, 'ISEC_HW_OC_Threshold_A', 'Value', '35.984');
out = sim(in);
[mosNodes, driverNodes] = gate_nodes(out.simlog);
highP01 = inf;
highMax = -inf;
offMiller = -inf;
turnOff = 0;
vdsMax = -inf;
vgsCache = cell(8,1);
timeCache = cell(8,1);
for q = 1:8
    vgsSeries = mosNodes{q}.voltage_gs.series;
    vdsSeries = mosNodes{q}.voltage_ds.series;
    commandSeries = driverNodes{q}.Driver_Vgs_Source.vT.series;
    t = time(vgsSeries); t = t(:);
    vgs = vgsSeries.values; vgs = vgs(:);
    vds = vdsSeries.values; vds = vds(:);
    command = commandSeries.values; command = command(:);
    commandHigh = command > 5;
    stableHigh = t >= 0.0003 & commandHigh;
    stableOff = t >= 0.0003 & ~commandHigh;
    edges = find([false; diff(commandHigh) ~= 0]);
    for edge = edges.'
        stableHigh = stableHigh & abs(t-t(edge)) > 400e-9;
        stableOff = stableOff & abs(t-t(edge)) > 120e-9;
    end
    assert(any(stableHigh), 'No stable high samples on Q%d.', q);
    assert(any(stableOff), 'No stable off samples on Q%d.', q);
    highP01 = min(highP01, prctile(vgs(stableHigh), 1));
    highMax = max(highMax, max(vgs(stableHigh)));
    offMiller = max(offMiller, max(vgs(stableOff)));
    turnOff = max(turnOff, turn_off_time(t, vgs, commandHigh));
    vdsMax = max(vdsMax, max(vds(t >= 0.0003)));
    vgsCache{q} = vgs;
    timeCache{q} = t;
end
overlap = false;
for pair = [1 2;3 4;5 6;7 8].'
    hi = pair(1); lo = pair(2);
    assert(isequal(timeCache{hi}, timeCache{lo}));
    overlap = overlap || any(vgsCache{hi} > 2 & vgsCache{lo} > 2);
end
ipri = signal(out, mdl + "_ipri_hf");
isec = signal(out, mdl + "_isec_hf");
rawFault = signal(out, mdl + "_raw_fault_n");
result = table(highP01, highMax, offMiller, turnOff*1e9, vdsMax, ...
    ~overlap, max(abs(ipri.values(ipri.time >= 0.0002))), ...
    max(abs(isec.values(isec.time >= 0.0002))), ...
    min(rawFault.values), false, 'VariableNames', {'VgsHighP01_V', ...
    'VgsHighMax_V','OffMillerMax_V','TurnOffTo1VMax_ns', ...
    'VdsRepeatMax_V','NoGateOverlap','IpriRawPeak_A', ...
    'IsecRawPeak_A','RawFaultNMin','Pass'});
result.Pass = result.VgsHighP01_V >= 9.9 && ...
    result.VgsHighMax_V <= 10.5 && result.OffMillerMax_V < 1 && ...
    result.TurnOffTo1VMax_ns <= 120 && result.VdsRepeatMax_V < 80 && ...
    result.NoGateOverlap && result.IpriRawPeak_A < 58.752 && ...
    result.IsecRawPeak_A < 35.984 && result.RawFaultNMin > 0.5;
disp(result);
end

function in = base_input(mdl, powerW, stopTime, maxStep, initialVout)
in = Simulink.SimulationInput(mdl);
in = in.setModelParameter('StopTime', sprintf('%.12g', stopTime), ...
    'MaxStep', sprintf('%.12g', maxStep), ...
    'ReturnWorkspaceOutputs', 'on');
in = set_named(in, mdl, 'VIN_18_32V_Bench_Source', 'dc_voltage', '24');
in = set_named(in, mdl, 'Rload_DC_Output', 'R', ...
    sprintf('%.12g', 48^2/powerW));
in = set_named(in, mdl, 'RUN_REQ_Model', 'Value', '1');
in = set_named(in, mdl, 'RAW_FAULT_N_Model', 'Value', '1');
for name = ["CSEC_DC_Link_Cap","CSEC_HF_MLCC_Bank", ...
        "COUT_DC_Bulk","COUT_HF_MLCC_Cap"]
    block = find_named(mdl, name);
    in = in.setBlockParameter(block, 'vc_specify', 'on');
    in = in.setBlockParameter(block, 'vc_priority', 'High');
    in = in.setBlockParameter(block, 'vc', sprintf('%.12g', initialVout));
    in = in.setBlockParameter(block, 'vc_unit', 'V');
end
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

function delay = turn_off_time(t, vgs, commandHigh)
falling = find(diff(commandHigh) == -1) + 1;
falling = falling(t(falling) >= 0.0003 & t(falling) <= t(end)-200e-9);
assert(~isempty(falling), 'No complete falling edge in analysis window.');
delays = zeros(numel(falling),1);
for n = 1:numel(falling)
    below = find(vgs(falling(n):end) <= 1, 1);
    assert(~isempty(below), 'Vgs did not fall below 1 V.');
    delays(n) = t(falling(n)+below-1)-t(falling(n));
end
delay = max(delays);
end

function [mosNodes, driverNodes] = gate_nodes(simlog)
p = simlog.PRI_Full_Bridge_RealDrivers;
s = simlog.SEC_Full_Bridge_RealDrivers;
mosNodes = {p.Leg_A_RealSwitchingCell.Q1_PRI_A_HS; ...
    p.Leg_A_RealSwitchingCell.Q2_PRI_A_LS; ...
    p.Leg_B_RealSwitchingCell.Q3_PRI_B_HS; ...
    p.Leg_B_RealSwitchingCell.Q4_PRI_B_LS; ...
    s.Leg_A_RealSwitchingCell.Q5_SEC_A_HS; ...
    s.Leg_A_RealSwitchingCell.Q6_SEC_A_LS; ...
    s.Leg_B_RealSwitchingCell.Q7_SEC_B_HS; ...
    s.Leg_B_RealSwitchingCell.Q8_SEC_B_LS};
driverNodes = {p.Leg_A_RealSwitchingCell.Q1_DRV_EQ; ...
    p.Leg_A_RealSwitchingCell.Q2_DRV_EQ; ...
    p.Leg_B_RealSwitchingCell.Q3_DRV_EQ; ...
    p.Leg_B_RealSwitchingCell.Q4_DRV_EQ; ...
    s.Leg_A_RealSwitchingCell.Q5_DRV_EQ; ...
    s.Leg_A_RealSwitchingCell.Q6_DRV_EQ; ...
    s.Leg_B_RealSwitchingCell.Q7_DRV_EQ; ...
    s.Leg_B_RealSwitchingCell.Q8_DRV_EQ};
end

function close_if_loaded(mdl)
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
end
