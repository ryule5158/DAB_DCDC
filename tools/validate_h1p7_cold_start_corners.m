function results = validate_h1p7_cold_start_corners()
%VALIDATE_H1P7_COLD_START_CORNERS Check limited startup over Vin and Llk.
rootDir = fileparts(fileparts(mfilename('fullpath')));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if ~bdIsLoaded(mdl)
    open_system(fullfile(notesDir, mdl + ".slx"));
end

vinValues = [18 24 28];
leakageValues = [0.4 0.5 0.6];
rows = repmat(empty_row(), numel(vinValues)*numel(leakageValues), 1);
rowIndex = 0;
for vin = vinValues
    for leakage = leakageValues
        rowIndex = rowIndex + 1;
        in = Simulink.SimulationInput(mdl);
        in = in.setModelParameter('StopTime', '0.00035', 'MaxStep', '5e-9', ...
            'RelTol', '5e-4', 'SimscapeLogType', 'all', ...
            'ReturnWorkspaceOutputs', 'on');
        in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":1"), ...
            'dc_voltage', text_value(vin));
        in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":40"), ...
            'l', text_value(leakage));
        in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":401"), ...
            'Value', '1');
        in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":402"), ...
            'Value', '1');
        for sid = [44 46 50 376]
            capPath = Simulink.ID.getFullName(mdl + ":" + sid);
            in = in.setBlockParameter(capPath, 'vc_specify', 'on');
            in = in.setBlockParameter(capPath, 'vc_priority', 'High');
            in = in.setBlockParameter(capPath, 'vc', '0');
            in = in.setBlockParameter(capPath, 'vc_unit', 'V');
        end

        out = sim(in);
        ipri = out.DAB_RA8P1_Final_Physical_MOSFET_ipri_hf;
        isec = out.DAB_RA8P1_Final_Physical_MOSFET_isec_hf;
        vout = out.DAB_RA8P1_Final_Physical_MOSFET_vout;
        ipriPeak = max(abs(ipri.signals.values), [], 'all');
        isecPeak = max(abs(isec.signals.values), [], 'all');
        [mosNodes, primaryIndices, secondaryIndices] = mos_nodes(out.simlog);
        vdsMax = -inf; secondaryVgsMax = -inf;
        vgsCache = cell(8, 1); timeCache = cell(8, 1);
        for q = 1:8
            vgsSeries = mosNodes{q}.voltage_gs.series;
            vdsSeries = mosNodes{q}.voltage_ds.series;
            t = time(vgsSeries); t = t(:);
            vgs = vgsSeries.values; vgs = vgs(:);
            vds = vdsSeries.values; vds = vds(:);
            vdsMax = max(vdsMax, max(vds, [], 'all'));
            if any(q == secondaryIndices)
                secondaryVgsMax = max(secondaryVgsMax, max(vgs, [], 'all'));
            end
            vgsCache{q} = vgs;
            timeCache{q} = t;
        end
        overlap = false; pairs = [1 2; 3 4; 5 6; 7 8];
        for leg = 1:4
            hi = pairs(leg, 1); lo = pairs(leg, 2);
            assert(isequal(timeCache{hi}, timeCache{lo}));
            overlap = overlap || any(vgsCache{hi} > 2 & vgsCache{lo} > 2);
        end
        primarySwitching = false;
        for q = primaryIndices
            primarySwitching = primarySwitching || any(vgsCache{q} > 2);
        end
        pass = primarySwitching && secondaryVgsMax < 1 && ...
            ipriPeak <= 35 && isecPeak <= 20 && vdsMax < 80 && ...
            ~overlap && vout.signals.values(end) > 0;
        rows(rowIndex) = struct('Vin_V', vin, 'Leakage_uH', leakage, ...
            'IpriPeak_A', ipriPeak, 'IsecPeak_A', isecPeak, ...
            'VoutFinal_V', vout.signals.values(end), ...
            'SecondaryVgsMax_V', secondaryVgsMax, ...
            'VdsRepeatMax_V', vdsMax, 'NoGateOverlap', ~overlap, ...
            'Pass', pass);
    end
end

results = struct2table(rows);
disp(results);
assert(all(results.Pass), 'H1.7 cold-start corner validation failed.');
writetable(results, fullfile(notesDir, 'validation_results', ...
    'DAB_RA8P1_H1p7_Cold_Start_Corners.csv'));
fprintf('PASS: H1.7 cold-start Vin/leakage corners.\n');
end

function [nodes, primaryIndices, secondaryIndices] = mos_nodes(simlog)
p = simlog.PRI_Full_Bridge_RealDrivers;
s = simlog.SEC_Full_Bridge_RealDrivers;
nodes = {p.Leg_A_RealSwitchingCell.Q1_PRI_A_HS; p.Leg_A_RealSwitchingCell.Q2_PRI_A_LS; ...
    p.Leg_B_RealSwitchingCell.Q3_PRI_B_HS; p.Leg_B_RealSwitchingCell.Q4_PRI_B_LS; ...
    s.Leg_A_RealSwitchingCell.Q5_SEC_A_HS; s.Leg_A_RealSwitchingCell.Q6_SEC_A_LS; ...
    s.Leg_B_RealSwitchingCell.Q7_SEC_B_HS; s.Leg_B_RealSwitchingCell.Q8_SEC_B_LS};
primaryIndices = 1:4;
secondaryIndices = 5:8;
end

function row = empty_row()
row = struct('Vin_V', NaN, 'Leakage_uH', NaN, ...
    'IpriPeak_A', NaN, 'IsecPeak_A', NaN, 'VoutFinal_V', NaN, ...
    'SecondaryVgsMax_V', NaN, 'VdsRepeatMax_V', NaN, ...
    'NoGateOverlap', false, 'Pass', false);
end

function value = text_value(value)
value = sprintf('%.12g', value);
end
