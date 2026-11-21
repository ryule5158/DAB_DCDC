function results = validate_h1p7_startup_voltage_stages()
%VALIDATE_H1P7_STARTUP_VOLTAGE_STAGES Check startup progress toward NORMAL_SPS.
rootDir = fileparts(fileparts(mfilename('fullpath')));
notesDir = fullfile(rootDir, 'docs', 'validation');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
if ~bdIsLoaded(mdl)
    open_system(fullfile(notesDir, mdl + ".slx"));
end

initialVoltages = [0 10 20 30 36 40 42];
rows = repmat(empty_row(), numel(initialVoltages), 1);
for k = 1:numel(initialVoltages)
    initialVoltage = initialVoltages(k);
    in = Simulink.SimulationInput(mdl);
    in = in.setModelParameter('StopTime', '0.00035', 'MaxStep', '5e-9', ...
        'RelTol', '5e-4', 'SimscapeLogType', 'all', ...
        'ReturnWorkspaceOutputs', 'on');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":1"), ...
        'dc_voltage', '24');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":55"), ...
        'R', text_value(48^2/100));
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":401"), ...
        'Value', '1');
    in = in.setBlockParameter(Simulink.ID.getFullName(mdl + ":402"), ...
        'Value', '1');
    for sid = [44 46 50 376]
        capPath = Simulink.ID.getFullName(mdl + ":" + sid);
        in = in.setBlockParameter(capPath, 'vc_specify', 'on');
        in = in.setBlockParameter(capPath, 'vc_priority', 'High');
        in = in.setBlockParameter(capPath, 'vc', text_value(initialVoltage));
        in = in.setBlockParameter(capPath, 'vc_unit', 'V');
    end

    out = sim(in);
    ipri = out.DAB_RA8P1_Final_Physical_MOSFET_ipri_hf;
    isec = out.DAB_RA8P1_Final_Physical_MOSFET_isec_hf;
    vout = out.DAB_RA8P1_Final_Physical_MOSFET_vout;
    phase = out.DAB_RA8P1_Final_Physical_MOSFET_phase_us;
    finalVoltage = vout.signals.values(end);
    ipriAbs = abs(ipri.signals.values);
    isecAbs = abs(isec.signals.values);
    [ipriPeakAll, ipriPeakIndex] = max(ipriAbs(:));
    isecPeakAll = max(isecAbs(:));
    settledIndex = ipri.time >= 50e-6;
    ipriPeakSettled = max(ipriAbs(settledIndex), [], 'all');
    isecPeakSettled = max(isecAbs(settledIndex), [], 'all');
    if initialVoltage == 0
        ipriPeakForPass = ipriPeakAll;
        isecPeakForPass = isecPeakAll;
    else
        ipriPeakForPass = ipriPeakSettled;
        isecPeakForPass = isecPeakSettled;
    end
    [secondaryVgsMax, allVgsMax, pulseCount] = gate_metrics(out.simlog);
    cycleEdges = (0:1/300e3:0.00035).';
    residualCurrent = interp1(ipri.time, abs(ipri.signals.values), ...
        cycleEdges, 'linear', 'extrap');
    normalSpsObserved = secondaryVgsMax > 2;
    pass = ipriPeakForPass <= 35 && isecPeakForPass <= 20 && ...
        allVgsMax <= 10.5 && ...
        ((initialVoltage < 40.5 && finalVoltage > initialVoltage) || ...
        (initialVoltage >= 40.5 && normalSpsObserved));
    rows(k) = struct('InitialVout_V', initialVoltage, ...
        'FinalVout_V', finalVoltage, ...
        'DeltaV_mV', 1000*(finalVoltage-initialVoltage), ...
        'IpriPeakAll_A', ipriPeakAll, 'IsecPeakAll_A', isecPeakAll, ...
        'IpriPeakTime_us', 1e6*ipri.time(ipriPeakIndex), ...
        'IpriPeakAfter50us_A', ipriPeakSettled, ...
        'IsecPeakAfter50us_A', isecPeakSettled, ...
        'MaxCommandPulse_ns', 1000*max(abs(phase.signals.values), [], 'all'), ...
        'PrimaryPulseCount', pulseCount, ...
        'ResidualMedian_A', median(residualCurrent), ...
        'CyclesResidualLt3', sum(residualCurrent < 3), ...
        'SecondaryVgsMax_V', secondaryVgsMax, ...
        'NormalSpsObserved', normalSpsObserved, 'Pass', pass);
end

results = struct2table(rows);
disp(results);
writetable(results, fullfile(notesDir, 'validation_results', ...
    'DAB_RA8P1_H1p7_Startup_Voltage_Stages.csv'));
assert(all(results.Pass), 'H1.7 startup voltage-stage validation failed.');
fprintf('PASS: H1.7 startup voltage stages.\n');
end

function [secondaryVgsMax, allVgsMax, pulseCount] = gate_metrics(simlog)
p = simlog.PRI_Full_Bridge_RealDrivers;
s = simlog.SEC_Full_Bridge_RealDrivers;
nodes = {p.Leg_A_RealSwitchingCell.Q1_PRI_A_HS; p.Leg_A_RealSwitchingCell.Q2_PRI_A_LS; ...
    p.Leg_B_RealSwitchingCell.Q3_PRI_B_HS; p.Leg_B_RealSwitchingCell.Q4_PRI_B_LS; ...
    s.Leg_A_RealSwitchingCell.Q5_SEC_A_HS; s.Leg_A_RealSwitchingCell.Q6_SEC_A_LS; ...
    s.Leg_B_RealSwitchingCell.Q7_SEC_B_HS; s.Leg_B_RealSwitchingCell.Q8_SEC_B_LS};
allVgsMax = -inf; secondaryVgsMax = -inf;
for q = 1:numel(nodes)
    vgsMax = max(nodes{q}.voltage_gs.series.values, [], 'all');
    allVgsMax = max(allVgsMax, vgsMax);
    if q >= 5, secondaryVgsMax = max(secondaryVgsMax, vgsMax); end
end
q1Values = nodes{1}.voltage_gs.series.values;
q3Values = nodes{3}.voltage_gs.series.values;
q1High = q1Values(:) > 2;
q3High = q3Values(:) > 2;
pulseCount = sum(diff([false; q1High]) == 1) + ...
    sum(diff([false; q3High]) == 1);
end

function row = empty_row()
row = struct('InitialVout_V', NaN, 'FinalVout_V', NaN, ...
    'DeltaV_mV', NaN, 'IpriPeakAll_A', NaN, 'IsecPeakAll_A', NaN, ...
    'IpriPeakTime_us', NaN, 'IpriPeakAfter50us_A', NaN, ...
    'IsecPeakAfter50us_A', NaN, ...
    'MaxCommandPulse_ns', NaN, ...
    'PrimaryPulseCount', NaN, 'ResidualMedian_A', NaN, ...
    'CyclesResidualLt3', NaN, ...
    'SecondaryVgsMax_V', NaN, 'NormalSpsObserved', false, 'Pass', false);
end

function value = text_value(value)
value = sprintf('%.12g', value);
end
