%% Validate the latest hardware-consistent DAB model three times
% MATLAB R2024b. This script does not tune the controller or modify blocks.

clear;
clc;

modelName = 'DAB_RA8P1_Final_Physical_MOSFET';
rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
modelFile = fullfile(notesDir, [modelName '.slx']);
if bdIsLoaded(modelName)
    close_system(modelName, 0);
end
load_system(modelFile);

runCount = 3;
windowStart_s = 6e-3;
windowEnd_s = 10e-3;
voltageTarget_V = 48;
averageTolerance_V = 1e-3;
rippleLimit_V = 0.1;

in(1:runCount) = Simulink.SimulationInput(modelName);
for runIndex = 1:runCount
    in(runIndex) = in(runIndex).setModelParameter('StopTime', '0.010');
end

out = sim(in);

run = (1:runCount).';
averageVoltage_V = zeros(runCount, 1);
averageError_mV = zeros(runCount, 1);
ripplePeakToPeak_V = zeros(runCount, 1);
outputCurrentMinimum_A = zeros(runCount, 1);
outputCurrentMaximum_A = zeros(runCount, 1);
passed = false(runCount, 1);

for runIndex = 1:runCount
    voltage = out(runIndex).DAB_RA8P1_Final_Physical_MOSFET_vout;
    voltageWindow = voltage.time >= windowStart_s & ...
        voltage.time <= windowEnd_s;
    voltageValues = voltage.signals.values(voltageWindow);

    outputCurrent = out(runIndex).DAB_RA8P1_Final_Physical_MOSFET_iout;
    currentWindow = outputCurrent.time >= windowStart_s & ...
        outputCurrent.time <= windowEnd_s;
    currentValues = outputCurrent.signals.values(currentWindow);

    averageVoltage_V(runIndex) = mean(voltageValues);
    averageError_mV(runIndex) = 1e3 * ...
        (averageVoltage_V(runIndex) - voltageTarget_V);
    ripplePeakToPeak_V(runIndex) = max(voltageValues) - min(voltageValues);
    outputCurrentMinimum_A(runIndex) = min(currentValues);
    outputCurrentMaximum_A(runIndex) = max(currentValues);
    passed(runIndex) = abs(averageVoltage_V(runIndex) - voltageTarget_V) < ...
        averageTolerance_V && ripplePeakToPeak_V(runIndex) < rippleLimit_V;
end

evidence = table(run, averageVoltage_V, averageError_mV, ...
    ripplePeakToPeak_V, outputCurrentMinimum_A, outputCurrentMaximum_A, passed);
disp(evidence);

assert(all(passed), ...
    'At least one run failed the 1mV average or 0.1Vpp ripple requirement.');

transformer = Simulink.ID.getFullName([modelName ':38']);
leakageInductor = Simulink.ID.getFullName([modelName ':40']);
commonModeCore = Simulink.ID.getFullName([modelName ':16']);
commonModePositiveDcr = Simulink.ID.getFullName([modelName ':14']);
commonModeNegativeDcr = Simulink.ID.getFullName([modelName ':15']);

assert(str2double(get_param(transformer, 'L1')) == 57.5);
assert(strcmp(get_param(transformer, 'L1_unit'), 'uH'));
assert(str2double(get_param(transformer, 'L2')) == 230);
assert(strcmp(get_param(transformer, 'L2_unit'), 'uH'));
assert(str2double(get_param(leakageInductor, 'l')) == 0.5);
assert(strcmp(get_param(leakageInductor, 'l_unit'), 'uH'));
assert(str2double(get_param(commonModeCore, 'L1')) == 2);
assert(str2double(get_param(commonModeCore, 'L2')) == 2);
assert(strcmp(get_param(commonModeCore, 'L1_unit'), 'mH'));
assert(str2double(get_param(commonModePositiveDcr, 'R')) == 0.0035);
assert(str2double(get_param(commonModeNegativeDcr, 'R')) == 0.0035);

outputFolder = fullfile(notesDir, 'final_physical_models_screenshots');
if ~isfolder(outputFolder)
    mkdir(outputFolder);
end
outputFile = fullfile(outputFolder, ...
    'DAB_RA8P1_Latest_Hardware_Model_Three_Run_Verification.csv');
writetable(evidence, outputFile);

fprintf('PASS: three runs meet the voltage and ripple requirements.\n');
fprintf('PASS: transformer and common-mode choke values remain locked.\n');
fprintf('Evidence: %s\n', outputFile);
close_system(modelName, 0);
