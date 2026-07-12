function results = validate_h1p6_gate_fine()
%VALIDATE_H1P6_GATE_FINE Run 5 ns gate/Vds checks inside entry hysteresis.
rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if ~bdIsLoaded(mdl)
    open_system(fullfile(notesDir, mdl + ".slx"));
end
cases = [18.5 44 300; 27.5 48 300; 21 46 500; 24 48 500];
rows = repmat(empty_row(), size(cases, 1), 1);
for k = 1:size(cases, 1)
    vin = cases(k, 1); vref = cases(k, 2); power = cases(k, 3);
    in = Simulink.SimulationInput(mdl);
    in = in.setModelParameter('StopTime', '0.00035', 'MaxStep', '5e-9', ...
        'RelTol', '5e-4', 'SimscapeLogType', 'all', ...
        'ReturnWorkspaceOutputs', 'on');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":1"), ...
        'dc_voltage', text_value(vin));
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":280"), ...
        'Value', text_value(vref));
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":55"), ...
        'R', text_value(vref^2/power));
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":401"), ...
        'Value', '1');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":402"), ...
        'Value', '1');
    for sid = [44 46 50 376]
        capPath = Simulink.ID.getFullName(mdl + ":" + sid);
        in = in.setBlockParameter(capPath, 'vc_specify', 'on');
        in = in.setBlockParameter(capPath, 'vc_priority', 'High');
        in = in.setBlockParameter(capPath, 'vc', text_value(vref));
        in = in.setBlockParameter(capPath, 'vc_unit', 'V');
    end
    out = sim(in);
    [mosNodes, driverNodes] = gate_nodes(out.simlog);
    vgsCache = cell(8, 1); timeCache = cell(8, 1);
    highP01 = inf; highMax = -inf; offMiller = -inf;
    turnOff = 0; vdsMax = -inf;
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
            stableHigh = stableHigh & abs(t-t(edge)) > 400e-9;
            stableOff = stableOff & abs(t-t(edge)) > 120e-9;
        end
        assert(any(stableHigh), 'No stable high samples for Q%d case %d.', q, k);
        assert(any(stableOff), 'No stable off samples for Q%d case %d.', q, k);
        highP01 = min(highP01, prctile(vgs(stableHigh), 1));
        highMax = max(highMax, max(vgs(stableHigh), [], 'all'));
        offMiller = max(offMiller, max(vgs(stableOff), [], 'all'));
        turnOff = max(turnOff, turn_off_time(t, vgs, commandHigh));
        vdsMax = max(vdsMax, max(vds(t >= 300e-6), [], 'all'));
        vgsCache{q} = vgs; timeCache{q} = t;
    end
    overlap = false; pairs = [1 2; 3 4; 5 6; 7 8];
    for leg = 1:4
        hi = pairs(leg, 1); lo = pairs(leg, 2);
        assert(isequal(timeCache{hi}, timeCache{lo}));
        overlap = overlap || any(vgsCache{hi} > 2 & vgsCache{lo} > 2);
    end
    pass = highP01 >= 9.9 && highMax <= 10.5 && offMiller < 1 && ...
        turnOff <= 120e-9 && vdsMax < 80 && ~overlap;
    rows(k) = struct('Vin_V', vin, 'Vref_V', vref, 'Power_W', power, ...
        'VgsHighP01_V', highP01, 'VgsHighMax_V', highMax, ...
        'OffMillerMax_V', offMiller, 'TurnOffTo1VMax_ns', turnOff*1e9, ...
        'VdsRepeatMax_V', vdsMax, 'NoGateOverlap', ~overlap, 'Pass', pass);
end
results = struct2table(rows);
disp(results);
assert(all(results.Pass), 'H1.6 fine gate validation failed.');
writetable(results, fullfile(notesDir, 'final_physical_models_screenshots', ...
    'DAB_RA8P1_H1p6_Gate_Fine_Check.csv'));
fprintf('PASS: H1.6 fine gate/Vds behavior.\n');
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
row = struct('Vin_V', NaN, 'Vref_V', NaN, 'Power_W', NaN, ...
    'VgsHighP01_V', NaN, 'VgsHighMax_V', NaN, 'OffMillerMax_V', NaN, ...
    'TurnOffTo1VMax_ns', NaN, 'VdsRepeatMax_V', NaN, ...
    'NoGateOverlap', false, 'Pass', false);
end

function value = text_value(value)
value = sprintf('%.12g', value);
end
