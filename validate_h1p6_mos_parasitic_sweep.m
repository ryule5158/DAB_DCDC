function results = validate_h1p6_mos_parasitic_sweep()
%VALIDATE_H1P6_MOS_PARASITIC_SWEEP Quantify package and short-trace parasitics.
rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if ~bdIsLoaded(mdl)
    open_system(fullfile(notesDir, mdl + ".slx"));
end

% [LD LS LG] in nH. The last row represents a deliberately poor gate loop.
corners = [0 0 0; 0.5 0.5 1.0; 1.0 1.0 2.0; 2.0 2.0 4.0];
mosSids = [74 75 115 116 166 167 207 208];
rows = repmat(empty_row(), size(corners, 1), 1);
for k = 1:size(corners, 1)
    in = Simulink.SimulationInput(mdl);
    in = in.setModelParameter('StopTime', '0.00035', 'MaxStep', '5e-9', ...
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
    for sid = mosSids
        mosPath = Simulink.ID.getFullName(mdl + ":" + sid);
        in = in.setBlockParameter(mosPath, 'LD', text_value(corners(k, 1)));
        in = in.setBlockParameter(mosPath, 'LS', text_value(corners(k, 2)));
        in = in.setBlockParameter(mosPath, 'LG', text_value(corners(k, 3)));
    end

    out = sim(in);
    [mosNodes, driverNodes] = gate_nodes(out.simlog);
    highMin = inf; highMax = -inf; offMiller = -inf;
    turnOff = 0; vdsMax = -inf; overlap = false;
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
        stableHigh = t >= 300e-6 & commandHigh;
        stableOff = t >= 300e-6 & ~commandHigh;
        edges = find([false; diff(commandHigh) ~= 0]);
        for edge = edges.'
            stableHigh = stableHigh & abs(t - t(edge)) > 400e-9;
            stableOff = stableOff & abs(t - t(edge)) > 120e-9;
        end
        assert(any(stableHigh) && any(stableOff), ...
            'Missing stable samples at parasitic corner %d, Q%d.', k, q);
        highMin = min(highMin, prctile(vgs(stableHigh), 1));
        highMax = max(highMax, max(vgs(stableHigh), [], 'all'));
        offMiller = max(offMiller, max(vgs(stableOff), [], 'all'));
        turnOff = max(turnOff, turn_off_time(t, vgs, commandHigh));
        vdsMax = max(vdsMax, max(vds(t >= 300e-6), [], 'all'));
        vgsCache{q} = vgs;
        timeCache{q} = t;
    end
    pairs = [1 2; 3 4; 5 6; 7 8];
    for leg = 1:4
        hi = pairs(leg, 1); lo = pairs(leg, 2);
        assert(isequal(timeCache{hi}, timeCache{lo}));
        overlap = overlap || any(vgsCache{hi} > 2 & vgsCache{lo} > 2);
    end
    pass = highMin >= 9.9 && highMax <= 10.5 && offMiller < 1 && ...
        turnOff <= 120e-9 && vdsMax < 80 && ~overlap;
    rows(k) = struct('LD_nH', corners(k, 1), 'LS_nH', corners(k, 2), ...
        'LG_nH', corners(k, 3), 'VgsHighP01_V', highMin, ...
        'VgsHighMax_V', highMax, 'OffMillerMax_V', offMiller, ...
        'TurnOffTo1VMax_ns', turnOff*1e9, 'VdsRepeatMax_V', vdsMax, ...
        'NoGateOverlap', ~overlap, 'Pass', pass);
end

results = struct2table(rows);
disp(results);
writetable(results, fullfile(notesDir, 'final_physical_models_screenshots', ...
    'DAB_RA8P1_H1p6_MOS_Parasitic_Sweep.csv'));
fprintf('MOS parasitic sweep complete.\n');
end

function delay = turn_off_time(t, vgs, commandHigh)
falling = find(diff(commandHigh) == -1) + 1;
falling = falling(t(falling) >= 300e-6);
if isempty(falling), delay = inf; return; end
delays = zeros(numel(falling), 1);
for n = 1:numel(falling)
    below = find(vgs(falling(n):end) <= 1, 1);
    if isempty(below), delays(n) = inf;
    else, delays(n) = t(falling(n)+below-1)-t(falling(n)); end
end
delay = max(delays);
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
row = struct('LD_nH', NaN, 'LS_nH', NaN, 'LG_nH', NaN, ...
    'VgsHighP01_V', NaN, 'VgsHighMax_V', NaN, 'OffMillerMax_V', NaN, ...
    'TurnOffTo1VMax_ns', NaN, 'VdsRepeatMax_V', NaN, ...
    'NoGateOverlap', false, 'Pass', false);
end

function value = text_value(value)
value = sprintf('%.12g', value);
end
