function results = validate_fixed_magnetics_operating_envelope()
%VALIDATE_FIXED_MAGNETICS_OPERATING_ENVELOPE Reproduce the H1.1 SPS limits.
%   The purchased transformer and common-mode choke are immutable. This
%   calculation checks only the normal SPS steady-state envelope; it does
%   not claim that zero-volt cold start or thermal performance is proven.

cfg.TurnsRatioSecPri = 2.0;
cfg.SwitchingFrequency_Hz = 300e3;
cfg.LsecNominal_H = 0.50e-6;
cfg.LsecMinimum_H = 0.40e-6; % Existing model tolerance: -20%.
cfg.UnmodeledPrimaryPeakMargin_A = 0.5;
cfg.PrimaryHardwareTripMinimum_A = 58.8;
cfg.PrimaryDesignPeakLimit_A = 50.0;

vinValues = (18:32).';
voutValues = (36:48).';
powerValues = [300; 500];
rowCount = numel(vinValues) * numel(voutValues) * numel(powerValues);

Vin_V = zeros(rowCount, 1);
Vout_V = zeros(rowCount, 1);
Ptarget_W = zeros(rowCount, 1);
Mismatch_V = zeros(rowCount, 1);
PhaseNominal_deg = nan(rowCount, 1);
IpriPkNominal_A = nan(rowCount, 1);
IpriRmsNominal_A = nan(rowCount, 1);
PhaseLmin_deg = nan(rowCount, 1);
IpriPkLmin_A = nan(rowCount, 1);
IpriRmsLmin_A = nan(rowCount, 1);
IpriPkDesign_A = nan(rowCount, 1);
MarginToMinHardwareTrip_A = nan(rowCount, 1);
CurrentLimitPass = false(rowCount, 1);
ModeRulePass = false(rowCount, 1);
Mode = strings(rowCount, 1);

row = 0;
for pIdx = 1:numel(powerValues)
    for vinIdx = 1:numel(vinValues)
        for voutIdx = 1:numel(voutValues)
            row = row + 1;
            vin = vinValues(vinIdx);
            vout = voutValues(voutIdx);
            ptarget = powerValues(pIdx);

            nominal = dabSpsCurrent(vin, vout, ptarget, ...
                cfg.LsecNominal_H, cfg);
            minimumL = dabSpsCurrent(vin, vout, ptarget, ...
                cfg.LsecMinimum_H, cfg);

            Vin_V(row) = vin;
            Vout_V(row) = vout;
            Ptarget_W(row) = ptarget;
            Mismatch_V(row) = abs(vout - cfg.TurnsRatioSecPri * vin);
            PhaseNominal_deg(row) = nominal.Phase_deg;
            IpriPkNominal_A(row) = nominal.IpriPk_A;
            IpriRmsNominal_A(row) = nominal.IpriRms_A;
            PhaseLmin_deg(row) = minimumL.Phase_deg;
            IpriPkLmin_A(row) = minimumL.IpriPk_A;
            IpriRmsLmin_A(row) = minimumL.IpriRms_A;
            IpriPkDesign_A(row) = minimumL.IpriPk_A + ...
                cfg.UnmodeledPrimaryPeakMargin_A;
            MarginToMinHardwareTrip_A(row) = ...
                cfg.PrimaryHardwareTripMinimum_A - IpriPkDesign_A(row);
            CurrentLimitPass(row) = minimumL.Feasible && ...
                IpriPkDesign_A(row) <= cfg.PrimaryDesignPeakLimit_A;

            if ptarget == 300
                Mode(row) = 'CONTINUOUS_300W';
                ModeRulePass(row) = vin >= 18 && vin <= 28 && ...
                    vout >= 36 && vout <= 48 && Mismatch_V(row) <= 8;
            else
                Mode(row) = 'SHORT_TEST_500W';
                ModeRulePass(row) = vin >= 21 && vin <= 24 && ...
                    vout >= 36 && vout <= 48 && Mismatch_V(row) <= 4;
            end
        end
    end
end

results = table(Vin_V, Vout_V, Ptarget_W, Mode, Mismatch_V, ...
    PhaseNominal_deg, IpriPkNominal_A, IpriRmsNominal_A, ...
    PhaseLmin_deg, IpriPkLmin_A, IpriRmsLmin_A, IpriPkDesign_A, ...
    MarginToMinHardwareTrip_A, CurrentLimitPass, ModeRulePass);

evidenceDir = fullfile(fileparts(mfilename('fullpath')), ...
    'DAB_RA8P1_PCB_Notes', 'final_physical_models_screenshots');
outputFile = fullfile(evidenceDir, ...
    'DAB_RA8P1_FixedMagnetics_OperatingEnvelope.csv');
writetable(results, outputFile);

printModeSummary(results, 'CONTINUOUS_300W');
printModeSummary(results, 'SHORT_TEST_500W');
fprintf('WROTE %s\n', outputFile);
end

function result = dabSpsCurrent(vin, vout, ptarget, lsec, cfg)
v1 = cfg.TurnsRatioSecPri * vin;
v2 = vout;
f = cfg.SwitchingFrequency_Hz;
period = 1 / f;
rootTerm = 1 - 8 * ptarget * f * lsec / (v1 * v2);

if rootTerm < 0
    result = struct('Feasible', false, 'Phase_deg', NaN, ...
        'IpriPk_A', NaN, 'IpriRms_A', NaN);
    return;
end

phase = pi / 2 * (1 - sqrt(rootTerm));
phaseTime = phase * period / (2 * pi);
i0 = -((v1 - v2) * period / 2 + 2 * v2 * phaseTime) / ...
    (2 * lsec);
i1 = i0 + (v1 + v2) * phaseTime / lsec;
i2 = -i0;
iSecPeak = max(abs([i0, i1, i2]));

segment1Integral = linearSquareIntegral(i0, i1, phaseTime);
segment2Integral = linearSquareIntegral(i1, i2, ...
    period / 2 - phaseTime);
iSecRms = sqrt(2 / period * (segment1Integral + segment2Integral));

result = struct( ...
    'Feasible', true, ...
    'Phase_deg', rad2deg(phase), ...
    'IpriPk_A', cfg.TurnsRatioSecPri * iSecPeak, ...
    'IpriRms_A', cfg.TurnsRatioSecPri * iSecRms);
end

function integral = linearSquareIntegral(startValue, endValue, duration)
integral = duration * (startValue^2 + startValue * endValue + ...
    endValue^2) / 3;
end

function printModeSummary(results, modeName)
mask = results.Mode == modeName & results.ModeRulePass;
selected = results(mask, :);
if isempty(selected)
    error('No cases selected for mode %s.', modeName);
end
[worstPeak, idx] = max(selected.IpriPkDesign_A);
fprintf(['MODE=%s CASES=%d WORST Vin=%.0fV Vout=%.0fV ' ...
    'IpriPkDesign=%.3fA MarginToMinTrip=%.3fA ALL_CURRENT_PASS=%d\n'], ...
    modeName, height(selected), selected.Vin_V(idx), ...
    selected.Vout_V(idx), worstPeak, ...
    selected.MarginToMinHardwareTrip_A(idx), ...
    all(selected.CurrentLimitPass));
end
