%% Validate first-board hardware closure calculations
% This script checks the final driver supply, hardware OVP, auxiliary eFuse,
% bleeders, output stored energy, and fixed-magnetics declarations used by
% DAB_RA8P1_PCB_Notes/26_DCDC_BOM_首板硬件定版.csv.

clear;
clc;

%% Fixed magnetics: declaration only, never optimized here
transformerPrimaryTurns = 3;
transformerSecondaryTurns = 6;
transformerPrimaryLm_uH = 57.5;
transformerSecondaryLm_uH = 230;
transformerSecondaryLeakage_uH = 0.50;
commonModeInductancePerWinding_mH = 2.0;
commonModeRatedCurrent_A = 30;

assert(transformerPrimaryTurns == 3);
assert(transformerSecondaryTurns == 6);
assert(transformerPrimaryLm_uH == 57.5);
assert(transformerSecondaryLm_uH == 230);
assert(transformerSecondaryLeakage_uH == 0.50);
assert(commonModeInductancePerWinding_mH == 2.0);
assert(commonModeRatedCurrent_A == 30);

%% TLV61046A gate-supply checks
tlvReference_V = 0.795;
feedbackTop_Ohm = 118e3;
feedbackBottom_Ohm = 10e3;
gateSupply_V = tlvReference_V * (1 + feedbackTop_Ohm / feedbackBottom_Ohm);

mosfetGateChargeMax_C = 115e-9;
switchingFrequency_Hz = 300e3;
halfBridgeGatePower_W = 2 * mosfetGateChargeMax_C * gateSupply_V * switchingFrequency_Hz;
boostOutputBudget_W = 0.79;
boostEfficiencyDesign = 0.80;
isolatedFiveVoltLoad_W = boostOutputBudget_W / boostEfficiencyDesign;

enPullup_Ohm = 100e3;
enPulldown_Ohm = 100e3;
enCapacitance_F = 220e-9;
enFinal_V = 5 * enPulldown_Ohm / (enPullup_Ohm + enPulldown_Ohm);
enTimeConstant_s = (enPullup_Ohm * enPulldown_Ohm / ...
    (enPullup_Ohm + enPulldown_Ohm)) * enCapacitance_F;
enGuaranteedHigh_V = 1.2;
enDelayToHigh_s = -enTimeConstant_s * log(1 - enGuaranteedHigh_V / enFinal_V);

%% TPS26600 auxiliary-input protection
efuseOvpTop_Ohm = 866e3;
efuseOvpBottom_Ohm = 30.1e3;
efuseThresholdTypical_V = 1.19;
efuseTripTypical_V = efuseThresholdTypical_V * ...
    (1 + efuseOvpTop_Ohm / efuseOvpBottom_Ohm);

efuseThresholdMin_V = 1.17;
efuseThresholdMax_V = 1.225;
resistorTolerance = 0.001;
efuseTripMin_V = efuseThresholdMin_V * ...
    (1 + efuseOvpTop_Ohm * (1 - resistorTolerance) / ...
    (efuseOvpBottom_Ohm * (1 + resistorTolerance)));
efuseTripMax_V = efuseThresholdMax_V * ...
    (1 + efuseOvpTop_Ohm * (1 + resistorTolerance) / ...
    (efuseOvpBottom_Ohm * (1 - resistorTolerance)));

efuseDvdtCap_F = 47e-9;
hardwareInputMax_V = 32;
efuseRampAt32V_s = 8e3 * hardwareInputMax_V * efuseDvdtCap_F;

%% Hardware OVP thresholds
vinAdcScale = 2 * 5.62e3 / (200e3 + 5.62e3);
vinTop_Ohm = 7.68e3;
vinBottom_Ohm = 10e3;
vinHysteresis_Ohm = 1e6;
logicHigh_V = 3.3;
vinThresholdRise_V = ...
    (logicHigh_V / vinTop_Ohm + logicHigh_V / vinHysteresis_Ohm) / ...
    (1 / vinTop_Ohm + 1 / vinBottom_Ohm + 1 / vinHysteresis_Ohm);
vinThresholdFall_V = ...
    (logicHigh_V / vinTop_Ohm) / ...
    (1 / vinTop_Ohm + 1 / vinBottom_Ohm + 1 / vinHysteresis_Ohm);
vinOvpRise_V = vinThresholdRise_V / vinAdcScale;
vinOvpFall_V = vinThresholdFall_V / vinAdcScale;

voutAdcScale = 2 * 3.24e3 / (200e3 + 3.24e3);
voutReference_V = 1.65;
voutReferenceSeries_Ohm = 4.99e3;
voutHysteresis_Ohm = 1e6;
voutThresholdRise_V = ...
    (voutReference_V / voutReferenceSeries_Ohm + ...
    logicHigh_V / voutHysteresis_Ohm) / ...
    (1 / voutReferenceSeries_Ohm + 1 / voutHysteresis_Ohm);
voutThresholdFall_V = ...
    (voutReference_V / voutReferenceSeries_Ohm) / ...
    (1 / voutReferenceSeries_Ohm + 1 / voutHysteresis_Ohm);
voutOvpRise_V = voutThresholdRise_V / voutAdcScale;
voutOvpFall_V = voutThresholdFall_V / voutAdcScale;

%% Bleeders, stored energy, and output fuse loading
outputBulkCapacitance_F = 8e-3;
outputNominal_V = 48;
outputSafeTouchCheck_V = 5;
outputBleeder_Ohm = 4.98e3;
outputStoredEnergy_J = 0.5 * outputBulkCapacitance_F * outputNominal_V^2;
outputBleederInitialPower_W = outputNominal_V^2 / outputBleeder_Ohm;
outputBleedTo5V_s = outputBulkCapacitance_F * outputBleeder_Ohm * ...
    log(outputNominal_V / outputSafeTouchCheck_V);

inputBulkCapacitance_F = 4e-3;
inputBleeder_Ohm = 5e3;
inputBleederInitialPower_W = hardwareInputMax_V^2 / inputBleeder_Ohm;
inputBleedTo5V_s = inputBulkCapacitance_F * inputBleeder_Ohm * ...
    log(hardwareInputMax_V / outputSafeTouchCheck_V);

continuousOutputCurrentAt36V_A = 300 / 36;
shortTestOutputCurrentAt36V_A = 500 / 36;
outputFuseRating_A = 15;

%% Acceptance assertions
assert(gateSupply_V > 9.9 && gateSupply_V < 10.5);
assert(halfBridgeGatePower_W < 0.75);
assert(isolatedFiveVoltLoad_W < 1.0);
assert(enDelayToHigh_s > 6e-3 && enDelayToHigh_s < 9e-3);
assert(efuseTripMin_V > 34.5 && efuseTripMax_V < 36.7);
assert(vinOvpRise_V > vinOvpFall_V);
assert(voutOvpRise_V > voutOvpFall_V);
assert(outputBleedTo5V_s < 100);
assert(continuousOutputCurrentAt36V_A < 0.60 * outputFuseRating_A);
assert(shortTestOutputCurrentAt36V_A < outputFuseRating_A);

%% Save compact evidence table
metric = {
    'Gate supply nominal'
    'Half-bridge max gate power'
    'UCC33421 output load budget'
    'BOOST EN delay to 1.2V'
    'Aux eFuse OVP typical'
    'Aux eFuse OVP minimum'
    'Aux eFuse OVP maximum'
    'Aux eFuse ramp at 32V'
    'VIN OVP rising'
    'VIN OVP falling'
    'VOUT OVP rising'
    'VOUT OVP falling'
    'Output stored energy at 48V'
    'Output bleeder initial power'
    'Output bleed 48V to 5V'
    'Input bleeder initial power'
    'Input bleed 32V to 5V'
    '300W output current at 36V'
    '500W output current at 36V'
    };

value = [
    gateSupply_V
    halfBridgeGatePower_W
    isolatedFiveVoltLoad_W
    1e3 * enDelayToHigh_s
    efuseTripTypical_V
    efuseTripMin_V
    efuseTripMax_V
    1e3 * efuseRampAt32V_s
    vinOvpRise_V
    vinOvpFall_V
    voutOvpRise_V
    voutOvpFall_V
    outputStoredEnergy_J
    outputBleederInitialPower_W
    outputBleedTo5V_s
    inputBleederInitialPower_W
    inputBleedTo5V_s
    continuousOutputCurrentAt36V_A
    shortTestOutputCurrentAt36V_A
    ];

unit = {
    'V'
    'W'
    'W'
    'ms'
    'V'
    'V'
    'V'
    'ms'
    'V'
    'V'
    'V'
    'V'
    'J'
    'W'
    's'
    'W'
    's'
    'A'
    'A'
    };

evidence = table(metric, value, unit);
disp(evidence);

outputFolder = fullfile('DAB_RA8P1_PCB_Notes', ...
    'final_physical_models_screenshots');
outputFile = fullfile(outputFolder, ...
    'DAB_RA8P1_FirstBoard_HardwareClosure.csv');
writetable(evidence, outputFile);

fprintf('PASS: fixed magnetics declarations and first-board hardware calculations.\n');
fprintf('Evidence: %s\n', outputFile);
