function results = validate_h1p6_snubber_states()
%VALIDATE_H1P6_SNUBBER_STATES Compare fitted RC snubbers with DNP hardware.
rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if ~bdIsLoaded(mdl)
    open_system(fullfile(notesDir, mdl + ".slx"));
end

cases = [27.5 48 300; 24 48 500];
stateNames = ["RC_10R_470pF"; "DNP_APPROX"];
capacitancePf = [470; 0.001];
snubberCSids = [91 107 132 148 183 199 224 240];
rows = repmat(empty_row(), size(cases, 1)*numel(stateNames), 1);
rowIndex = 0;
for stateIndex = 1:numel(stateNames)
    for caseIndex = 1:size(cases, 1)
        rowIndex = rowIndex + 1;
        vin = cases(caseIndex, 1);
        vref = cases(caseIndex, 2);
        power = cases(caseIndex, 3);
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
        for sid = snubberCSids
            capPath = Simulink.ID.getFullName(mdl + ":" + sid);
            in = in.setBlockParameter(capPath, 'c', text_value(capacitancePf(stateIndex)));
        end

        out = sim(in);
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
            stableOff = t >= 300e-6 & ~commandHigh;
            edges = find([false; diff(commandHigh) ~= 0]);
            for edge = edges.'
                stableOff = stableOff & abs(t - t(edge)) > 120e-9;
            end
            assert(any(stableOff), 'Missing stable off samples.');
            offMiller = max(offMiller, max(vgs(stableOff), [], 'all'));
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
        rows(rowIndex) = struct('SnubberState', stateNames(stateIndex), ...
            'Vin_V', vin, 'Vref_V', vref, 'Power_W', power, ...
            'OffMillerMax_V', offMiller, 'VdsRepeatMax_V', vdsMax, ...
            'NoGateOverlap', ~overlap, ...
            'Pass80V', vdsMax < 80 && offMiller < 1 && ~overlap);
    end
end

results = struct2table(rows);
disp(results);
writetable(results, fullfile(notesDir, 'final_physical_models_screenshots', ...
    'DAB_RA8P1_H1p6_Snubber_State_Check.csv'));
fprintf('Snubber state comparison complete.\n');
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
row = struct('SnubberState', "", 'Vin_V', NaN, 'Vref_V', NaN, ...
    'Power_W', NaN, 'OffMillerMax_V', NaN, 'VdsRepeatMax_V', NaN, ...
    'NoGateOverlap', false, 'Pass80V', false);
end

function value = text_value(value)
value = sprintf('%.12g', value);
end
