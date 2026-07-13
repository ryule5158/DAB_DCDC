%% Validate H1.7 first-board hardware closure calculations
% Checks the frozen magnetics, gate-supply derating, control-island power,
% hardware protection thresholds, output filter, bleeders, and fuse loading.

clear;
clc;
rootDir = fileparts(fileparts(mfilename('fullpath')));

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

ucc33421Limit85C_W = 1.5;
ucc33421Limit105C_W = 1.0;
ucc33421Limit125C_W = 0.5;
ucc33421Margin85C_W = ucc33421Limit85C_W - isolatedFiveVoltLoad_W;
ucc33421Margin105C_W = ucc33421Limit105C_W - isolatedFiveVoltLoad_W;

enPullup_Ohm = 100e3;
enPulldown_Ohm = 100e3;
enCapacitance_F = 220e-9;
enFinal_V = 5 * enPulldown_Ohm / (enPullup_Ohm + enPulldown_Ohm);
enTimeConstant_s = (enPullup_Ohm * enPulldown_Ohm / ...
    (enPullup_Ohm + enPulldown_Ohm)) * enCapacitance_F;
enGuaranteedHigh_V = 1.2;
enDelayToHigh_s = -enTimeConstant_s * log(1 - enGuaranteedHigh_V / enFinal_V);

%% Control-island supply split introduced in H1.7
amc3302MaxCurrentPerDevice_A = 40e-3;
amc3330MaxCurrentPerDevice_A = 41e-3;
amcFiveVoltCurrentMax_A = 2 * amc3302MaxCurrentPerDevice_A + ...
    2 * amc3330MaxCurrentPerDevice_A;
amcFiveVoltPowerMax_W = 5 * amcFiveVoltCurrentMax_A;
fiveVoltSenseFerriteRating_A = 0.5;

tlv773DesignLoadMax_A = 0.100;
tlv773Power_W = (5 - 3.3) * tlv773DesignLoadMax_A;
tlv773ThetaJa_CperW = 242.5;
tlv773LocalAmbientLimit_C = 60;
tlv773JunctionAtDesignLimit_C = tlv773LocalAmbientLimit_C + ...
    tlv773Power_W * tlv773ThetaJa_CperW;

%% TMCS1133 hardware overcurrent thresholds
tmcsSupply_V = 3.3;
tmcsSensitivity_VperA = 20e-3;
tmcsVocInputResistance_Ohm = 120e3;
tmcsThresholdTolerance = 0.10;

tmcsPrimaryTop_Ohm = 10e3;
tmcsPrimaryBottom_Ohm = 1.91e3;
tmcsPrimaryBottomEffective_Ohm = 1 / ...
    (1 / tmcsPrimaryBottom_Ohm + 1 / tmcsVocInputResistance_Ohm);
tmcsPrimaryVoc_V = tmcsSupply_V * tmcsPrimaryBottomEffective_Ohm / ...
    (tmcsPrimaryTop_Ohm + tmcsPrimaryBottomEffective_Ohm);
tmcsPrimaryTrip_A = 2.5 * tmcsPrimaryVoc_V / tmcsSensitivity_VperA;
tmcsPrimaryTripMin_A = tmcsPrimaryTrip_A * (1 - tmcsThresholdTolerance);
tmcsPrimaryTripMax_A = tmcsPrimaryTrip_A * (1 + tmcsThresholdTolerance);

tmcsSecondaryTop_Ohm = 9.24e3;
tmcsSecondaryBottom_Ohm = 1.00e3;
tmcsSecondaryBottomEffective_Ohm = 1 / ...
    (1 / tmcsSecondaryBottom_Ohm + 1 / tmcsVocInputResistance_Ohm);
tmcsSecondaryVoc_V = tmcsSupply_V * tmcsSecondaryBottomEffective_Ohm / ...
    (tmcsSecondaryTop_Ohm + tmcsSecondaryBottomEffective_Ohm);
tmcsSecondaryTrip_A = 2.5 * tmcsSecondaryVoc_V / tmcsSensitivity_VperA;
tmcsSecondaryTripMin_A = tmcsSecondaryTrip_A * (1 - tmcsThresholdTolerance);
tmcsSecondaryTripMax_A = tmcsSecondaryTrip_A * (1 + tmcsThresholdTolerance);

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

%% Output inductor and switching-node stress declarations
loutInductance_uH = 2.2;
loutDcrMax_Ohm = 2.05e-3;
loutIsat10Percent_A = 100;
% The datasheet specifies both the 20% and 30% drop currents as >100 A.
% Store 100 A here as the conservative numeric lower bound for evidence export.
loutIsat20And30PercentLowerBound_A = 100;
loutIrms20Rise_A = 20;
loutIrms40Rise_A = 30;
loutCopperLoss300W36V_W = continuousOutputCurrentAt36V_A^2 * loutDcrMax_Ohm;
loutCopperLoss500W36V_W = shortTestOutputCurrentAt36V_A^2 * loutDcrMax_Ohm;

mosfetVdsRating_V = 100;
repetitiveVdsDesignLimit_V = 80;
bridgeBusLoopTarget_nH = 10;
busSweepFile = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes', ...
    'validation_results', ...
    'DAB_RA8P1_H1p6_Bus_Loop_Sweep.csv');
assert(isfile(busSweepFile), 'Run validate_h1p6_bus_loop_sweep first.');
busSweep = readtable(busSweepFile);
targetBusRow = busSweep.LoopInductance_nH == bridgeBusLoopTarget_nH;
negativeBusRow = busSweep.LoopInductance_nH == 80;
assert(nnz(targetBusRow) == 1 && nnz(negativeBusRow) == 1);
validatedVdsAt10nH_V = busSweep.VdsRepeatMax_V(targetBusRow);
validatedIpriAt10nH_A = busSweep.IpriPeak_A(targetBusRow);
validatedIsecAt10nH_A = busSweep.IsecPeak_A(targetBusRow);
negativeVdsAt80nH_V = busSweep.VdsRepeatMax_V(negativeBusRow);

%% Acceptance assertions
assert(gateSupply_V > 9.9 && gateSupply_V < 10.5);
assert(halfBridgeGatePower_W < 0.75);
assert(isolatedFiveVoltLoad_W < 1.0);
assert(ucc33421Margin85C_W > 0.5);
assert(ucc33421Margin105C_W > 0);
assert(isolatedFiveVoltLoad_W > ucc33421Limit125C_W);
assert(amcFiveVoltCurrentMax_A < fiveVoltSenseFerriteRating_A);
assert(tlv773JunctionAtDesignLimit_C < 125);
assert(tmcsPrimaryTrip_A > 64 && tmcsPrimaryTrip_A < 67);
assert(tmcsSecondaryTrip_A > 39 && tmcsSecondaryTrip_A < 41);
assert(tmcsPrimaryTripMin_A > 58);
assert(tmcsSecondaryTripMin_A > 35);
assert(enDelayToHigh_s > 6e-3 && enDelayToHigh_s < 9e-3);
assert(efuseTripMin_V > 34.5 && efuseTripMax_V < 36.7);
assert(vinOvpRise_V > vinOvpFall_V);
assert(voutOvpRise_V > voutOvpFall_V);
assert(outputBleedTo5V_s < 100);
assert(continuousOutputCurrentAt36V_A < 0.60 * outputFuseRating_A);
assert(shortTestOutputCurrentAt36V_A < outputFuseRating_A);
assert(shortTestOutputCurrentAt36V_A < loutIrms20Rise_A);
assert(loutIsat10Percent_A >= 100);
assert(loutIsat20And30PercentLowerBound_A >= 100);
assert(validatedVdsAt10nH_V < repetitiveVdsDesignLimit_V);
assert(validatedIpriAt10nH_A >= 60 && validatedIsecAt10nH_A >= 30);
assert(~busSweep.Pass80V(negativeBusRow));
assert(repetitiveVdsDesignLimit_V <= 0.8 * mosfetVdsRating_V);

%% Save compact evidence table
metric = {
    'Gate supply nominal'
    'Half-bridge max gate power'
    'UCC33421 output load budget'
    'UCC33421 margin at 85C'
    'UCC33421 margin at 105C'
    'AMC four-device max current on 5V_SENSE'
    'AMC four-device max power on 5V_SENSE'
    'TLV773 design load limit'
    'TLV773 dissipation at design limit'
    'TLV773 estimated junction at 60C ambient'
    'TMCS primary VOC'
    'TMCS primary trip nominal'
    'TMCS primary trip minimum including IC tolerance'
    'TMCS primary trip maximum including IC tolerance'
    'TMCS secondary VOC'
    'TMCS secondary trip nominal'
    'TMCS secondary trip minimum including IC tolerance'
    'TMCS secondary trip maximum including IC tolerance'
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
    'LOUT inductance nominal'
    'LOUT 10 percent inductance-drop current'
    'LOUT 20/30 percent inductance-drop current lower bound'
    'LOUT copper loss at 300W 36V'
    'LOUT copper loss at 500W 36V'
    'Validated repetitive Vds at 10nH loop'
    'Validated primary commutation peak at 10nH loop'
    'Validated secondary commutation peak at 10nH loop'
    'Negative-control Vds at 80nH loop'
    'Repetitive Vds design limit'
    'Bridge bus-loop inductance target'
    };

value = [
    gateSupply_V
    halfBridgeGatePower_W
    isolatedFiveVoltLoad_W
    ucc33421Margin85C_W
    ucc33421Margin105C_W
    1e3 * amcFiveVoltCurrentMax_A
    amcFiveVoltPowerMax_W
    1e3 * tlv773DesignLoadMax_A
    tlv773Power_W
    tlv773JunctionAtDesignLimit_C
    tmcsPrimaryVoc_V
    tmcsPrimaryTrip_A
    tmcsPrimaryTripMin_A
    tmcsPrimaryTripMax_A
    tmcsSecondaryVoc_V
    tmcsSecondaryTrip_A
    tmcsSecondaryTripMin_A
    tmcsSecondaryTripMax_A
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
    loutInductance_uH
    loutIsat10Percent_A
    loutIsat20And30PercentLowerBound_A
    loutCopperLoss300W36V_W
    loutCopperLoss500W36V_W
    validatedVdsAt10nH_V
    validatedIpriAt10nH_A
    validatedIsecAt10nH_A
    negativeVdsAt80nH_V
    repetitiveVdsDesignLimit_V
    bridgeBusLoopTarget_nH
    ];

unit = {
    'V'
    'W'
    'W'
    'W'
    'W'
    'mA'
    'W'
    'mA'
    'W'
    'degC'
    'V'
    'A'
    'A'
    'A'
    'V'
    'A'
    'A'
    'A'
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
    'uH'
    'A'
    'A'
    'W'
    'W'
    'V'
    'A'
    'A'
    'V'
    'V'
    'nH'
    };

evidence = table(metric, value, unit);
disp(evidence);

outputFolder = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes', ...
    'validation_results');
outputFile = fullfile(outputFolder, ...
    'DAB_RA8P1_FirstBoard_HardwareClosure.csv');
writetable(evidence, outputFile);

fprintf('PASS: H1.7 fixed magnetics, power, protection, and output-filter calculations.\n');
fprintf('Evidence: %s\n', outputFile);
