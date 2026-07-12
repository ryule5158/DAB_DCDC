function [deviceResults, caseResults] = validate_final_simplified_gate_drive()
% Validate the final 0/10.18 V UCC21520 gate network in R2024b.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
evidenceDir = fullfile(notesDir, 'final_physical_models_screenshots');
if ~isfolder(evidenceDir)
    mkdir(evidenceDir);
end

mdl = "DAB_RA8P1_Final_Physical_MOSFET";
modelFile = fullfile(notesDir, mdl + ".slx");
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end
load_system(modelFile);

% Cases remain inside the frozen hardware operating rules.
cases = [
    18 44 300
    28 48 300
    21 46 500
    24 48 500
    ];

deviceRows = repmat(empty_device_row(), size(cases, 1) * 8, 1);
caseRows = repmat(empty_case_row(), size(cases, 1), 1);
row = 0;

for caseIndex = 1:size(cases, 1)
    vin = cases(caseIndex, 1);
    vref = cases(caseIndex, 2);
    power = cases(caseIndex, 3);
    rload = vref^2 / power;

    in = Simulink.SimulationInput(mdl);
    in = in.setModelParameter( ...
        'StopTime', '0.00045', ...
        'SimscapeLogType', 'all', ...
        'ReturnWorkspaceOutputs', 'on');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":1"), ...
        'dc_voltage', sprintf('%.10g', vin));
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":280"), ...
        'Value', sprintf('%.10g', vref));
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":55"), ...
        'R', sprintf('%.10g', rload));

    % Precharge is used only for switching-stress characterization. It does
    % not claim that the cold-start sequence has been validated.
    capSids = [44 46 50 376];
    for sid = capSids
        capPath = Simulink.ID.getFullName(mdl + ":" + sid);
        in = in.setBlockParameter(capPath, 'vc_specify', 'on');
        in = in.setBlockParameter(capPath, 'vc_priority', 'High');
        in = in.setBlockParameter(capPath, 'vc', sprintf('%.10g', vref));
        in = in.setBlockParameter(capPath, 'vc_unit', 'V');
    end

    out = sim(in);
    [mosNodes, driverNodes] = gate_nodes(out.simlog);
    vgsCache = cell(8, 1);
    timeCache = cell(8, 1);

    for deviceIndex = 1:8
        row = row + 1;
        vgsSeries = mosNodes{deviceIndex}.voltage_gs.series;
        vdsSeries = mosNodes{deviceIndex}.voltage_ds.series;
        cmdSeries = driverNodes{deviceIndex}.Driver_Vgs_Source.vT.series;

        t = time(vgsSeries);
        t = t(:);
        vgs = vgsSeries.values;
        vgs = vgs(:);
        vds = vdsSeries.values;
        vds = vds(:);
        cmd = cmdSeries.values;
        cmd = cmd(:);
        vgsCache{deviceIndex} = vgs;
        timeCache{deviceIndex} = t;

        commandHigh = cmd > 5;
        stableHigh = t >= 50e-6 & commandHigh;
        stableOff = t >= 50e-6 & ~commandHigh;
        edgeIndex = find([false; diff(commandHigh) ~= 0]);
        for edge = edgeIndex.'
            % The middle of the on pulse represents the fully enhanced
            % high level. The earlier interval contains the real Miller
            % plateau and must not be mislabeled as a low supply rail.
            stableHigh = stableHigh & abs(t - t(edge)) > 400e-9;
            stableOff = stableOff & abs(t - t(edge)) > 120e-9;
        end

        highMin = min(vgs(stableHigh));
        highP01 = prctile(vgs(stableHigh), 1);
        highBelow9p9 = 100 * nnz(vgs(stableHigh) < 9.9) / nnz(stableHigh);
        highMax = max(vgs(stableHigh));
        offMillerMax = max(vgs(stableOff));
        vdsMax = max(vds(t >= 50e-6));
        turnOffMax = maximum_turn_off_time(t, vgs, commandHigh);

        deviceRows(row) = struct( ...
            'CaseIndex', caseIndex, ...
            'Vin_V', vin, ...
            'VoutCommand_V', vref, ...
            'Power_W', power, ...
            'Device', "Q" + deviceIndex, ...
            'VgsHighTransientMin_V', highMin, ...
            'VgsHighP01_V', highP01, ...
            'VgsHighBelow9p9_pct', highBelow9p9, ...
            'VgsHighMax_V', highMax, ...
            'OffMillerMax_V', offMillerMax, ...
            'TurnOffTo1VMax_ns', turnOffMax * 1e9, ...
            'VdsRepeatMax_V', vdsMax, ...
            'GatePass', highP01 >= 9.9 && highBelow9p9 < 1.0 && ...
                highMax <= 10.5 && ...
                offMillerMax < 1.0 && turnOffMax <= 120e-9, ...
            'VdsPass', vdsMax < 80);
    end

    overlap = false;
    legPairs = [1 2; 3 4; 5 6; 7 8];
    for leg = 1:size(legPairs, 1)
        hi = legPairs(leg, 1);
        lo = legPairs(leg, 2);
        assert(isequal(timeCache{hi}, timeCache{lo}), ...
            'Gate log time bases differ in leg %d.', leg);
        overlap = overlap || any(vgsCache{hi} > 2.0 & vgsCache{lo} > 2.0);
    end

    caseMask = [deviceRows.CaseIndex] == caseIndex;
    caseRows(caseIndex) = struct( ...
        'CaseIndex', caseIndex, ...
        'Vin_V', vin, ...
        'VoutCommand_V', vref, ...
        'Power_W', power, ...
        'Mode', select_mode(power), ...
        'NoGateOverlap', ~overlap, ...
        'AllGatePass', all([deviceRows(caseMask).GatePass]), ...
        'AllVdsPass', all([deviceRows(caseMask).VdsPass]), ...
        'CasePass', ~overlap && all([deviceRows(caseMask).GatePass]) && ...
            all([deviceRows(caseMask).VdsPass]));
end

deviceResults = struct2table(deviceRows);
caseResults = struct2table(caseRows);
writetable(deviceResults, fullfile(evidenceDir, ...
    'DAB_RA8P1_Final_Simplified_Gate_Device_Check.csv'));
writetable(caseResults, fullfile(evidenceDir, ...
    'DAB_RA8P1_Final_Simplified_Gate_Case_Check.csv'));

disp(caseResults);
fprintf(['Worst transient Miller plateau during high command: %.6f V\n' ...
    'Fully enhanced Vgs P01 to maximum: %.6f to %.6f V\n' ...
    'Largest fraction below 9.9 V during high command: %.6f %%\n' ...
    'Worst off-state Miller uplift: %.6f V\n' ...
    'Worst turn-off to 1 V: %.3f ns\n' ...
    'Worst repetitive Vds: %.6f V\n'], ...
    min(deviceResults.VgsHighTransientMin_V), ...
    min(deviceResults.VgsHighP01_V), max(deviceResults.VgsHighMax_V), ...
    max(deviceResults.VgsHighBelow9p9_pct), ...
    max(deviceResults.OffMillerMax_V), ...
    max(deviceResults.TurnOffTo1VMax_ns), ...
    max(deviceResults.VdsRepeatMax_V));

assert(all(caseResults.CasePass), ...
    'At least one simplified gate-drive case failed.');
close_system(mdl, 0);
end

function delay = maximum_turn_off_time(t, vgs, commandHigh)
fallingEdges = find(diff(commandHigh) == -1) + 1;
fallingEdges = fallingEdges(t(fallingEdges) >= 50e-6);
delays = zeros(numel(fallingEdges), 1);
for index = 1:numel(fallingEdges)
    edge = fallingEdges(index);
    below = find(vgs(edge:end) <= 1.0, 1, 'first');
    if isempty(below)
        delays(index) = inf;
    else
        delays(index) = t(edge + below - 1) - t(edge);
    end
end
delay = max(delays);
end

function mode = select_mode(power)
if power == 300
    mode = "CONTINUOUS_300W";
else
    mode = "SHORT_TEST_500W";
end
end

function [mosNodes, driverNodes] = gate_nodes(simlog)
p = simlog.PRI_Full_Bridge_RealDrivers;
s = simlog.SEC_Full_Bridge_RealDrivers;
mosNodes = {
    p.Leg_A_RealSwitchingCell.Q1_PRI_A_HS
    p.Leg_A_RealSwitchingCell.Q2_PRI_A_LS
    p.Leg_B_RealSwitchingCell.Q3_PRI_B_HS
    p.Leg_B_RealSwitchingCell.Q4_PRI_B_LS
    s.Leg_A_RealSwitchingCell.Q5_SEC_A_HS
    s.Leg_A_RealSwitchingCell.Q6_SEC_A_LS
    s.Leg_B_RealSwitchingCell.Q7_SEC_B_HS
    s.Leg_B_RealSwitchingCell.Q8_SEC_B_LS
    };
driverNodes = {
    p.Leg_A_RealSwitchingCell.Q1_DRV_EQ
    p.Leg_A_RealSwitchingCell.Q2_DRV_EQ
    p.Leg_B_RealSwitchingCell.Q3_DRV_EQ
    p.Leg_B_RealSwitchingCell.Q4_DRV_EQ
    s.Leg_A_RealSwitchingCell.Q5_DRV_EQ
    s.Leg_A_RealSwitchingCell.Q6_DRV_EQ
    s.Leg_B_RealSwitchingCell.Q7_DRV_EQ
    s.Leg_B_RealSwitchingCell.Q8_DRV_EQ
    };
end

function row = empty_device_row()
row = struct( ...
    'CaseIndex', NaN, ...
    'Vin_V', NaN, ...
    'VoutCommand_V', NaN, ...
    'Power_W', NaN, ...
    'Device', "", ...
    'VgsHighTransientMin_V', NaN, ...
    'VgsHighP01_V', NaN, ...
    'VgsHighBelow9p9_pct', NaN, ...
    'VgsHighMax_V', NaN, ...
    'OffMillerMax_V', NaN, ...
    'TurnOffTo1VMax_ns', NaN, ...
    'VdsRepeatMax_V', NaN, ...
    'GatePass', false, ...
    'VdsPass', false);
end

function row = empty_case_row()
row = struct( ...
    'CaseIndex', NaN, ...
    'Vin_V', NaN, ...
    'VoutCommand_V', NaN, ...
    'Power_W', NaN, ...
    'Mode', "", ...
    'NoGateOverlap', false, ...
    'AllGatePass', false, ...
    'AllVdsPass', false, ...
    'CasePass', false);
end
