function results = run_dab_siminput_matrix_20260814(profile)
%RUN_DAB_SIMINPUT_MATRIX_20260814 Read-only operating-point validation.
%   Uses Simulink.SimulationInput overrides only. The source SLX is never
%   saved or structurally modified.
%
%   Profiles:
%     "smoke25"  - one 24 V / 48 V / 25 ohm case
%     "corner25" - four VIN/VREF corners at 25 ohm
%     "matrix25" - VIN=[18 21 24], VREF=[36 42 48], R=25 ohm
%     "matrix25_vin18|21|24" - one VIN row at 25 ohm
%     "bench_priority" - 21V/42V/20ohm and 24V/48V/24.3,15ohm
%     "rated300" - same matrix with R=VREF^2/300 W
%     "all"      - matrix25 followed by rated300

if nargin < 1
    profile = "matrix25";
end
profile = string(profile);

rootDir = fileparts(fileparts(mfilename('fullpath')));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
modelName = "DAB_RA8P1_Final_Physical_MOSFET";
modelFile = fullfile(notesDir, modelName + ".slx");
assert(isfile(modelFile), 'Model file not found: %s', modelFile);

cases = make_cases(profile);
stamp = string(datetime('now','Format','yyyyMMdd_HHmmss'));
evidenceDir = fullfile(notesDir, 'validation_results', ...
    "siminput_20260814_" + profile + "_" + stamp);
if ~isfolder(evidenceDir)
    mkdir(evidenceDir);
end

if bdIsLoaded(modelName)
    close_system(modelName, 0);
end
load_system(modelFile);
cleanup = onCleanup(@() close_if_loaded(modelName)); %#ok<NASGU>

% Resolve by SID rather than constructing paths from display names.
sourceBlock = Simulink.ID.getFullName(modelName + ":1");
setpointBlock = Simulink.ID.getFullName(modelName + ":280");
loadBlock = Simulink.ID.getFullName(modelName + ":55");
outputCapSids = ["46","50","44","376"];
outputCapBlocks = strings(size(outputCapSids));
for n = 1:numel(outputCapSids)
    outputCapBlocks(n) = string(Simulink.ID.getFullName( ...
        modelName + ":" + outputCapSids(n)));
end
inputCapSids = ["23","19","18","20"];
inputCapBlocks = strings(size(inputCapSids));
for n = 1:numel(inputCapSids)
    inputCapBlocks(n) = string(Simulink.ID.getFullName( ...
        modelName + ":" + inputCapSids(n)));
end
toWorkspaceBlocks = find_system(modelName, 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'BlockType', 'ToWorkspace');

modelInfo = dir(modelFile);
metadata = struct();
metadata.profile = char(profile);
metadata.model_file = modelFile;
metadata.model_bytes = modelInfo.bytes;
metadata.model_timestamp = char(datetime(modelInfo.datenum, ...
    'ConvertFrom','datenum','Format','yyyy-MM-dd HH:mm:ss'));
metadata.stop_time_s = 0.03;
metadata.steady_window_s = 0.002;
metadata.load_25ohm_note = ['Fixed 25 ohm load; nominal target powers are ' ...
    '51.84 W at 36 V, 70.56 W at 42 V, and 92.16 W at 48 V.'];
metadata.rated300_note = 'Target-dependent resistive load R=Vref^2/300 W.';
metadata.convergence_rule = ['abs(mean(Vout)-Vref)<=0.5 V, ' ...
    'Vout p-p<=1.0 V, abs(Vout slope)<=100 V/s, ' ...
    'driver enable>0.5, fault latch N>0.5, and observable normal-mode ' ...
    'phase range in final 2 ms.'];
metadata.mode_rule = ['Controller source selects EPS when filtered abs(Iout)>=4 A; ' ...
    'otherwise SPS after normal-mode entry. Mode below is inferred from final Iout.'];
write_json(fullfile(evidenceDir, 'metadata.json'), metadata);

rows = repmat(empty_row(), 0, 1);
for k = 1:height(cases)
    c = cases(k,:);
    fprintf('\nCASE_BEGIN index=%d/%d VIN=%.3f VREF=%.3f R=%.6f profile=%s\n', ...
        k, height(cases), c.Vin_V, c.Vref_V, c.Rload_ohm, c.LoadProfile);
    simIn = Simulink.SimulationInput(modelName);
    simIn = simIn.setBlockParameter(sourceBlock, 'dc_voltage', ...
        sprintf('%.12g', c.Vin_V));
    simIn = simIn.setBlockParameter(setpointBlock, 'Value', ...
        sprintf('%.12g', c.Vref_V));
    simIn = simIn.setBlockParameter(loadBlock, 'R', ...
        sprintf('%.12g', c.Rload_ohm));
    % The saved model has all four output-bus capacitors initialized at
    % 48 V. For a fair local working-point test, initialize them at the
    % requested Vref using ephemeral SimulationInput overrides.
    for n = 1:numel(outputCapBlocks)
        simIn = simIn.setBlockParameter(outputCapBlocks(n), 'vc', ...
            sprintf('%.12g', c.Vref_V));
    end
    % Likewise, the saved input bus capacitors are initialized at 24 V.
    % Match their local operating-point initial voltage to commanded VIN.
    for n = 1:numel(inputCapBlocks)
        simIn = simIn.setBlockParameter(inputCapBlocks(n), 'vc', ...
            sprintf('%.12g', c.Vin_V));
    end
    simIn = simIn.setModelParameter('StopTime', '0.03', ...
        'SimulationMode', 'normal', 'ReturnWorkspaceOutputs', 'on');

    % Ephemeral logging reduction. Keep HF-current resolution denser so the
    % reported peak remains useful, although it is not hardware sign-off.
    for n = 1:numel(toWorkspaceBlocks)
        variableName = string(get_param(toWorkspaceBlocks{n}, 'VariableName'));
        decimation = '20';
        if contains(variableName, ["ipri_hf","isec_hf"])
            decimation = '5';
        end
        simIn = simIn.setBlockParameter(toWorkspaceBlocks{n}, ...
            'Decimation', decimation);
    end

    row = empty_row();
    row.CaseIndex = k;
    row.LoadProfile = c.LoadProfile;
    row.Vin_Command_V = c.Vin_V;
    row.Vref_V = c.Vref_V;
    row.Rload_ohm = c.Rload_ohm;
    row.TargetPower_W = c.TargetPower_W;
    ticCase = tic;
    try
        simOut = sim(simIn);
        row.Runtime_s = toc(ticCase);
        [row, steady] = summarize_case(simOut, row, metadata.steady_window_s);
        evidenceFile = fullfile(evidenceDir, sprintf( ...
            'case_%02d_vin_%g_vref_%g_%s.mat', k, c.Vin_V, c.Vref_V, ...
            char(lower(c.LoadProfile))));
        save(evidenceFile, 'steady', 'row', 'metadata', '-v7.3');
        row.EvidenceMAT = string(evidenceFile);
        fprintf(['CASE_RESULT index=%d status=%s mode=%s Vout=%.6f V ' ...
            'err=%+.6f V Pin=%.6f W Pout=%.6f W eff=%.4f pct ' ...
            'phase=%.6f us ripple=%.6f V slope=%+.3f V/s ' ...
            'IpriPk=%.4f A IsecPk=%.4f A runtime=%.1f s\n'], ...
            k, row.Status, row.Mode, row.Vout_mean_V, row.Vout_error_V, ...
            row.Pin_mean_W, row.Pout_mean_W, row.Efficiency_ratio_pct, ...
            row.Phase_mean_us, row.Vout_pp_V, row.Vout_slope_V_per_s, ...
            row.Ipri_peak_A, row.Isec_peak_A, row.Runtime_s);
    catch ME
        row.Runtime_s = toc(ticCase);
        row.Status = "ERROR";
        row.ErrorID = string(ME.identifier);
        row.ErrorMessage = string(getReport(ME, 'extended', ...
            'hyperlinks', 'off'));
        fprintf('CASE_ERROR index=%d id=%s runtime=%.1f s\n%s\n', ...
            k, row.ErrorID, row.Runtime_s, row.ErrorMessage);
    end
    rows(end+1,1) = row; %#ok<AGROW>
    results = struct2table(rows);
    writetable(results, fullfile(evidenceDir, 'summary_incremental.csv'));
    save(fullfile(evidenceDir, 'summary_incremental.mat'), ...
        'results', 'metadata', 'cases');
    fprintf('CASE_END index=%d summary=%s\n', k, ...
        fullfile(evidenceDir, 'summary_incremental.csv'));
end

results = struct2table(rows);
summaryCsv = fullfile(evidenceDir, 'summary.csv');
writetable(results, summaryCsv);
save(fullfile(evidenceDir, 'summary.mat'), 'results', 'metadata', 'cases');
fprintf('\nRUN_COMPLETE profile=%s cases=%d summary=%s\n', ...
    profile, height(results), summaryCsv);
end

function cases = make_cases(profile)
switch profile
    case "smoke25"
        vin = 24; vref = 48; loadProfile = "R25";
    case "corner25"
        vin = [18;18;24;24];
        vref = [36;48;36;48];
        loadProfile = repmat("R25", 4, 1);
    case "matrix25"
        [vinGrid,vrefGrid] = ndgrid([18 21 24],[36 42 48]);
        vin = vinGrid(:); vref = vrefGrid(:);
        loadProfile = repmat("R25", numel(vin), 1);
    case {"matrix25_vin18","matrix25_vin21","matrix25_vin24"}
        vinValue = str2double(extractAfter(profile,"matrix25_vin"));
        vin = repmat(vinValue,3,1);
        vref = [36;42;48];
        loadProfile = repmat("R25",3,1);
    case "bench_priority"
        vin = [21;24;24];
        vref = [42;48;48];
        rload = [20;24.3;15];
        targetPower = (vref.^2)./rload;
        loadProfile = ["R20";"R24p3";"R15"];
        cases = table(vin, vref, rload, targetPower, loadProfile, ...
            'VariableNames', {'Vin_V','Vref_V','Rload_ohm', ...
            'TargetPower_W','LoadProfile'});
        return;
    case "rated300"
        [vinGrid,vrefGrid] = ndgrid([18 21 24],[36 42 48]);
        vin = vinGrid(:); vref = vrefGrid(:);
        loadProfile = repmat("P300", numel(vin), 1);
    case "all"
        [vinGrid,vrefGrid] = ndgrid([18 21 24],[36 42 48]);
        vin = [vinGrid(:);vinGrid(:)];
        vref = [vrefGrid(:);vrefGrid(:)];
        loadProfile = [repmat("R25",9,1);repmat("P300",9,1)];
    otherwise
        error('Unknown profile: %s', profile);
end
rload = 25*ones(size(vin));
targetPower = (vref.^2)./rload;
isRated = loadProfile == "P300";
rload(isRated) = (vref(isRated).^2)./300;
targetPower(isRated) = 300;
cases = table(vin, vref, rload, targetPower, loadProfile, ...
    'VariableNames', {'Vin_V','Vref_V','Rload_ohm','TargetPower_W','LoadProfile'});
end

function [row, steady] = summarize_case(simOut, row, windowSeconds)
names = struct( ...
    'vin', "DAB_RA8P1_Final_Physical_MOSFET_vin", ...
    'vout', "DAB_RA8P1_Final_Physical_MOSFET_vout", ...
    'iin', "DAB_RA8P1_Final_Physical_MOSFET_iin", ...
    'iout', "DAB_RA8P1_Final_Physical_MOSFET_iout", ...
    'pin', "DAB_RA8P1_Final_Physical_MOSFET_pin", ...
    'pout', "DAB_RA8P1_Final_Physical_MOSFET_pout", ...
    'eff', "DAB_RA8P1_Final_Physical_MOSFET_efficiency", ...
    'phase', "DAB_RA8P1_Final_Physical_MOSFET_phase_us", ...
    'ipri', "DAB_RA8P1_Final_Physical_MOSFET_ipri_hf", ...
    'isec', "DAB_RA8P1_Final_Physical_MOSFET_isec_hf", ...
    'driver', "DAB_RA8P1_Final_Physical_MOSFET_driver_enable", ...
    'faultn', "DAB_RA8P1_Final_Physical_MOSFET_fault_latch_n");
fields = fieldnames(names);
steady = struct();
for k = 1:numel(fields)
    f = fields{k};
    [t,y] = extract_series(simOut, names.(f));
    tEnd = t(end);
    keep = t >= max(t(1), tEnd-windowSeconds);
    steady.(f) = struct('time', t(keep), 'values', y(keep));
end

row.Vin_mean_V = mean(steady.vin.values,'omitnan');
row.Vout_mean_V = mean(steady.vout.values,'omitnan');
row.Vout_error_V = row.Vout_mean_V-row.Vref_V;
row.Vout_pp_V = max(steady.vout.values)-min(steady.vout.values);
row.Iin_mean_A = mean(steady.iin.values,'omitnan');
row.Iout_mean_A = mean(steady.iout.values,'omitnan');
row.Pin_mean_W = mean(steady.pin.values,'omitnan');
row.Pout_mean_W = mean(steady.pout.values,'omitnan');
row.Efficiency_model_pct = mean(steady.eff.values,'omitnan');
if row.Pin_mean_W > 0.2
    row.Efficiency_ratio_pct = 100*max(row.Pout_mean_W,0)/row.Pin_mean_W;
else
    row.Efficiency_ratio_pct = NaN;
end
row.Phase_mean_us = mean(steady.phase.values,'omitnan');
row.Phase_min_us = min(steady.phase.values);
row.Phase_max_us = max(steady.phase.values);
row.Ipri_peak_A = max(abs(steady.ipri.values));
row.Isec_peak_A = max(abs(steady.isec.values));
row.DriverEnable_min = min(steady.driver.values);
row.FaultLatchN_min = min(steady.faultn.values);
if numel(steady.vout.time) >= 2
    t0 = steady.vout.time(1);
    p = polyfit(steady.vout.time-t0, steady.vout.values, 1);
    row.Vout_slope_V_per_s = p(1);
else
    row.Vout_slope_V_per_s = NaN;
end
% Startup reports its primary pulse width through PhaseUs and can reach
% 1.3 us, whereas normal SPS is clamped to [-0.55, 0.46] us and EPS to
% [0.28, 0.32] us. Use this observable to avoid mislabeling a voltage that
% is merely being held by startup pulses as a normal SPS working point.
normal = row.DriverEnable_min > 0.5 && row.FaultLatchN_min > 0.5 && ...
    row.Vout_mean_V >= 35.5 && max(abs(steady.phase.values)) <= 0.55;
if normal && abs(row.Iout_mean_A) >= 4
    row.Mode = "EPS-inferred";
elseif normal
    row.Mode = "SPS-inferred";
else
    row.Mode = "STARTUP_or_OFF";
end
isConverged = abs(row.Vout_error_V) <= 0.5 && row.Vout_pp_V <= 1.0 && ...
    abs(row.Vout_slope_V_per_s) <= 100 && row.DriverEnable_min > 0.5 && ...
    row.FaultLatchN_min > 0.5 && normal;
if isConverged
    row.Status = "PASS";
else
    row.Status = "FAIL";
end
end

function [t,y] = extract_series(simOut, variableName)
value = simOut.get(char(variableName));
if isa(value, 'timeseries')
    t = value.Time;
    y = value.Data;
elseif isstruct(value) && isfield(value,'time') && isfield(value,'signals')
    t = value.time;
    y = value.signals.values;
else
    error('Unsupported logged type for %s: %s', variableName, class(value));
end
t = double(t(:));
y = double(squeeze(y));
if ~isvector(y)
    y = y(:,1);
end
y = y(:);
assert(numel(t)==numel(y), 'Time/data length mismatch for %s', variableName);
end

function row = empty_row()
row = struct( ...
    'CaseIndex', NaN, 'LoadProfile', "", ...
    'Vin_Command_V', NaN, 'Vin_mean_V', NaN, ...
    'Vref_V', NaN, 'Rload_ohm', NaN, 'TargetPower_W', NaN, ...
    'Vout_mean_V', NaN, 'Vout_error_V', NaN, 'Vout_pp_V', NaN, ...
    'Vout_slope_V_per_s', NaN, 'Iin_mean_A', NaN, 'Iout_mean_A', NaN, ...
    'Pin_mean_W', NaN, 'Pout_mean_W', NaN, ...
    'Efficiency_model_pct', NaN, 'Efficiency_ratio_pct', NaN, ...
    'Phase_mean_us', NaN, 'Phase_min_us', NaN, 'Phase_max_us', NaN, ...
    'Ipri_peak_A', NaN, 'Isec_peak_A', NaN, ...
    'DriverEnable_min', NaN, 'FaultLatchN_min', NaN, ...
    'Mode', "", 'Status', "", 'Runtime_s', NaN, ...
    'EvidenceMAT', "", 'ErrorID', "", 'ErrorMessage', "");
end

function write_json(fileName, value)
fid = fopen(fileName, 'w');
assert(fid >= 0, 'Unable to open metadata file: %s', fileName);
cleanup = onCleanup(@() fclose(fid)); %#ok<NASGU>
fprintf(fid, '%s\n', jsonencode(value, PrettyPrint=true));
end

function close_if_loaded(modelName)
if bdIsLoaded(modelName)
    close_system(modelName, 0);
end
end
