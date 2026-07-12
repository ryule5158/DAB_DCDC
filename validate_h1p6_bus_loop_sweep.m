function results = validate_h1p6_bus_loop_sweep()
%VALIDATE_H1P6_BUS_LOOP_SWEEP Establish the PCB commutation-loop limit.
rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if ~bdIsLoaded(mdl)
    open_system(fullfile(notesDir, mdl + ".slx"));
end

railInductanceNh = [2.5 5 10 20 40];
busSids = [403 404 405 406];
stopTime = 2.6e-3;
analysisStart = stopTime - 100e-6;
rows = repmat(empty_row(), numel(railInductanceNh), 1);
for k = 1:numel(railInductanceNh)
    in = Simulink.SimulationInput(mdl);
    in = in.setModelParameter('StopTime', text_value(stopTime), 'MaxStep', '1e-8', ...
        'RelTol', '5e-4', 'SimscapeLogType', 'all', ...
        'ReturnWorkspaceOutputs', 'on');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":1"), ...
        'dc_voltage', '24');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":280"), ...
        'Value', '48');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":55"), ...
        'R', text_value(48^2/500));
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":401"), ...
        'Value', '1');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":402"), ...
        'Value', '1');
    for sid = [44 46 50 376]
        capPath = Simulink.ID.getFullName(mdl + ":" + sid);
        in = in.setBlockParameter(capPath, 'vc_specify', 'on');
        in = in.setBlockParameter(capPath, 'vc_priority', 'High');
        in = in.setBlockParameter(capPath, 'vc', '48');
        in = in.setBlockParameter(capPath, 'vc_unit', 'V');
    end
    for sid = busSids
        busPath = Simulink.ID.getFullName(mdl + ":" + sid);
        in = in.setBlockParameter(busPath, 'l', text_value(railInductanceNh(k)));
    end

    out = sim(in);
    ipri = out.DAB_RA8P1_Final_Physical_MOSFET_ipri_hf;
    isec = out.DAB_RA8P1_Final_Physical_MOSFET_isec_hf;
    phase = out.DAB_RA8P1_Final_Physical_MOSFET_phase_us;
    ipriPeak = max(abs(ipri.signals.values(ipri.time >= analysisStart)), [], 'all');
    isecPeak = max(abs(isec.signals.values(isec.time >= analysisStart)), [], 'all');
    phaseAbsMax = max(abs(phase.signals.values(phase.time >= analysisStart)), [], 'all');
    [mosNodes, driverNodes] = gate_nodes(out.simlog);
    vdsMax = -inf; offMiller = -inf; overlap = false;
    vgsCache = cell(8, 1); timeCache = cell(8, 1);
    for q = 1:8
        vgsSeries = mosNodes{q}.voltage_gs.series;
        vdsSeries = mosNodes{q}.voltage_ds.series;
        cmdSeries = driverNodes{q}.Driver_Vgs_Source.vT.series;
        t = time(vgsSeries); t = t(:);
        vgs = vgsSeries.values; vgs = vgs(:);
        vds = vdsSeries.values; vds = vds(:);
        cmd = cmdSeries.values; cmd = cmd(:);
        commandHigh = cmd > 5;
        stableOff = t >= analysisStart & ~commandHigh;
        edges = find([false; diff(commandHigh) ~= 0]);
        edges = edges(t(edges) >= analysisStart - 200e-9);
        for edge = edges.'
            stableOff = stableOff & abs(t - t(edge)) > 120e-9;
        end
        assert(any(stableOff), 'Missing stable off samples.');
        offMiller = max(offMiller, max(vgs(stableOff), [], 'all'));
        vdsMax = max(vdsMax, max(vds(t >= analysisStart), [], 'all'));
        vgsCache{q} = vgs;
        timeCache{q} = t;
    end
    pairs = [1 2; 3 4; 5 6; 7 8];
    for leg = 1:4
        hi = pairs(leg, 1); lo = pairs(leg, 2);
        assert(isequal(timeCache{hi}, timeCache{lo}));
        overlap = overlap || any(vgsCache{hi} > 2 & vgsCache{lo} > 2);
    end
    rows(k) = struct('RailInductance_nH', railInductanceNh(k), ...
        'LoopInductance_nH', 2*railInductanceNh(k), ...
        'PhaseAbsMax_us', phaseAbsMax, 'IpriPeak_A', ipriPeak, ...
        'IsecPeak_A', isecPeak, ...
        'OffMillerMax_V', offMiller, 'VdsRepeatMax_V', vdsMax, ...
        'NoGateOverlap', ~overlap, ...
        'Pass80V', vdsMax < 80 && offMiller < 1 && ~overlap);
end

results = struct2table(rows);
disp(results);
writetable(results, fullfile(notesDir, 'final_physical_models_screenshots', ...
    'DAB_RA8P1_H1p6_Bus_Loop_Sweep.csv'));
assert(all(diff(results.VdsRepeatMax_V) > 0), ...
    'Vds peak must increase monotonically with bus-loop inductance.');
targetRows = results.LoopInductance_nH <= 10;
assert(all(results.IpriPeak_A(targetRows) >= 60) && ...
    all(results.IsecPeak_A(targetRows) >= 30), ...
    'Bus-loop sweep did not reach the intended high-current commutation stress.');
assert(all(results.Pass80V(results.LoopInductance_nH <= 10)), ...
    'The frozen <=10 nH PCB bus-loop target must pass the 80 V limit.');
assert(~results.Pass80V(results.LoopInductance_nH == 80), ...
    'The 80 nH negative-control case must fail and expose lost Vds margin.');
fprintf('PASS: PCB bus-loop target and negative-control boundary.\n');
end

function [mosNodes, driverNodes] = gate_nodes(simlog)
p = simlog.PRI_Full_Bridge_RealDrivers;
s = simlog.SEC_Full_Bridge_RealDrivers;
mosNodes = {p.Leg_A_RealSwitchingCell.Q1_PRI_A_HS; p.Leg_A_RealSwitchingCell.Q2_PRI_A_LS; ...
    p.Leg_B_RealSwitchingCell.Q3_PRI_B_HS; p.Leg_B_RealSwitchingCell.Q4_PRI_B_LS; ...
    s.Leg_A_RealSwitchingCell.Q5_SEC_A_HS; s.Leg_A_RealSwitchingCell.Q6_SEC_A_LS; ...
    s.Leg_B_RealSwitchingCell.Q7_SEC_B_HS; s.Leg_B_RealSwitchingCell.Q8_SEC_B_LS};
driverNodes = {p.Leg_A_RealSwitchingCell.Q1_DRV_EQ; p.Leg_A_RealSwitchingCell.Q2_DRV_EQ; ...
    p.Leg_B_RealSwitchingCell.Q3_DRV_EQ; p.Leg_B_RealSwitchingCell.Q4_DRV_EQ; ...
    s.Leg_A_RealSwitchingCell.Q5_DRV_EQ; s.Leg_A_RealSwitchingCell.Q6_DRV_EQ; ...
    s.Leg_B_RealSwitchingCell.Q7_DRV_EQ; s.Leg_B_RealSwitchingCell.Q8_DRV_EQ};
end

function row = empty_row()
row = struct('RailInductance_nH', NaN, 'LoopInductance_nH', NaN, ...
    'PhaseAbsMax_us', NaN, 'IpriPeak_A', NaN, 'IsecPeak_A', NaN, ...
    'OffMillerMax_V', NaN, 'VdsRepeatMax_V', NaN, ...
    'NoGateOverlap', false, 'Pass80V', false);
end

function value = text_value(value)
value = sprintf('%.12g', value);
end
