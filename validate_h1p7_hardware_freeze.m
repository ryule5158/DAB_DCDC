function results = validate_h1p7_hardware_freeze()
%VALIDATE_H1P7_HARDWARE_FREEZE Cross-check model, BOM, and drawing documents.

rootDir = fileparts(mfilename('fullpath'));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
mdl = "DAB_RA8P1_Final_Physical_MOSFET";
mdlFile = fullfile(notesDir, mdl + ".slx");
if ~bdIsLoaded(mdl)
    open_system(mdlFile);
end

name = strings(0, 1);
actual = strings(0, 1);
expected = strings(0, 1);
pass = false(0, 1);

    function record(checkName, actualValue, expectedValue, condition)
        name(end + 1, 1) = string(checkName);
        actual(end + 1, 1) = string(actualValue);
        expected(end + 1, 1) = string(expectedValue);
        pass(end + 1, 1) = logical(condition);
    end

    function record_numeric(checkName, value, target, tolerance)
        record(checkName, sprintf('%.12g', value), ...
            sprintf('%.12g +/- %.3g', target, tolerance), ...
            abs(value - target) <= tolerance);
    end

%% Saved-model electrical parameters
record_numeric('Transformer primary magnetizing inductance uH', ...
    numeric_param(mdl, 38, 'L1'), 57.5, 1e-12);
record_numeric('Transformer secondary magnetizing inductance uH', ...
    numeric_param(mdl, 38, 'L2'), 230, 1e-12);
record_numeric('Transformer coupling coefficient', ...
    numeric_param(mdl, 38, 'k'), 0.999999, 1e-12);
record_numeric('Transformer secondary leakage uH', ...
    numeric_param(mdl, 40, 'L'), 0.5, 1e-12);
record_numeric('Input bleeder Ohm', numeric_param(mdl, 21, 'R'), ...
    5000, 1e-9);
record_numeric('Input damping capacitance uF', ...
    numeric_param(mdl, 23, 'C'), 40, 1e-12);
record_numeric('Input damping series resistance Ohm', ...
    numeric_param(mdl, 22, 'R'), 0.33, 1e-12);

record_numeric('CSEC bulk uF', numeric_param(mdl, 44, 'C'), 94, 1e-12);
record_numeric('CSEC bulk ESR Ohm', numeric_param(mdl, 44, 'R'), ...
    0.056, 1e-12);
record_numeric('CSEC HF effective uF', numeric_param(mdl, 376, 'C'), ...
    40, 1e-12);
record_numeric('LOUT uH', numeric_param(mdl, 45, 'L'), 2.2, 1e-12);
record_numeric('LOUT DCR Ohm', numeric_param(mdl, 45, 'R'), ...
    0.00205, 1e-12);
record_numeric('COUT bulk uF', numeric_param(mdl, 46, 'C'), 8000, 1e-9);
record_numeric('COUT bulk ESR Ohm', numeric_param(mdl, 46, 'R'), ...
    0.003875, 1e-12);
record_numeric('COUT HF effective uF', numeric_param(mdl, 50, 'C'), ...
    40, 1e-12);
record_numeric('Output bleeder Ohm', numeric_param(mdl, 51, 'R'), ...
    4980, 1e-9);

for sid = 403:406
    record_numeric(sprintf('PCB rail inductance SID %d nH', sid), ...
        numeric_param(mdl, sid, 'L'), 5, 1e-12);
end
for sid = [408, 412]
    record_numeric(sprintf('TMCS conductor SID %d Ohm', sid), ...
        numeric_param(mdl, sid, 'R'), 0.0007, 1e-12);
end
record_numeric('Saved RUN_REQ default', numeric_param(mdl, 401, 'Value'), ...
    0, 0);
record_numeric('Saved RAW_FAULT_N default', ...
    numeric_param(mdl, 402, 'Value'), 1, 0);

%% Eight identical switching cells
record_block_group(mdl, 'GateScale', 'Gain', 1.018, 8, ...
    'Eight gate command scales', @record);
record_block_group(mdl, 'GateBias', 'Bias', 0, 8, ...
    'Eight gate command biases', @record);
record_block_group(mdl, 'Rg_On', 'R', 4.7, 8, ...
    'Eight turn-on resistors', @record);
record_block_group(mdl, 'Rg_Off', 'R', 2.2, 8, ...
    'Eight turn-off resistors', @record);
record_block_group(mdl, 'Gate_Pulldown', 'R', 10000, 8, ...
    'Eight gate-source pull-downs', @record);
record_block_group(mdl, 'UCC_Rout_Source_1p15Ohm', 'R', 1.15, 8, ...
    'Eight UCC source impedances', @record);
record_block_group(mdl, 'UCC_Rout_Sink_0p55Ohm', 'R', 0.55, 8, ...
    'Eight UCC sink impedances', @record);
record_block_group(mdl, 'DS_Snubber_R', 'R', 10, 8, ...
    'Eight snubber resistors', @record);
record_block_group(mdl, 'DS_Snubber_C', 'C', 470, 8, ...
    'Eight snubber capacitors pF', @record);

mosBlocks = find_system(mdl, 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'MaskType', 'N-Channel MOSFET');
record('AONS66917 MOSFET block count', numel(mosBlocks), '8', ...
    numel(mosBlocks) == 8);
if numel(mosBlocks) == 8
    mosOk = true;
    for k = 1:numel(mosBlocks)
        mosOk = mosOk && abs(str2double(get_param(mosBlocks{k}, 'Rds')) - 0.0029) < 1e-12;
        mosOk = mosOk && abs(str2double(get_param(mosBlocks{k}, 'Rds_T2')) - 0.0048) < 1e-12;
        mosOk = mosOk && abs(str2double(get_param(mosBlocks{k}, 'LD')) - 1) < 1e-12;
        mosOk = mosOk && abs(str2double(get_param(mosBlocks{k}, 'LS')) - 1) < 1e-12;
        mosOk = mosOk && abs(str2double(get_param(mosBlocks{k}, 'LG')) - 2) < 1e-12;
        mosOk = mosOk && abs(str2double(get_param(mosBlocks{k}, 'TT')) - 43e-9) < 1e-15;
        mosOk = mosOk && contains(get_param(mosBlocks{k}, ...
            'AttributesFormatString'), 'AONS66917');
    end
    record('AONS66917 electrical/parasitic identity', mosOk, ...
        'Rds 2.9/4.8mOhm, LD/LS/LG 1/1/2nH, trr 43ns', mosOk);
end

allNames = string(get_param(find_system(mdl, 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'Type', 'Block'), 'Name'));
forbiddenBlock = contains(lower(allNames), 'qfast') | ...
    contains(lower(allNames), 'pnp') | ...
    contains(lower(allNames), 'output_common_mode') | ...
    contains(lower(allNames), 'ltransfer_link');
record('Forbidden model blocks', sum(forbiddenBlock), '0', ...
    ~any(forbiddenBlock));

%% H1.7 BOM
bomFile = fullfile(notesDir, '26_DCDC_BOM_首板硬件定版.csv');
opts = detectImportOptions(bomFile, 'Encoding', 'UTF-8', ...
    'VariableNamingRule', 'preserve');
bom = readtable(bomFile, opts);
record('BOM row count', height(bom), '179', height(bom) == 179);
record('BOM column count', width(bom), '10', width(bom) == 10);
refdes = string(bom.("位号"));
record('BOM blank reference designators', sum(strlength(refdes) == 0), ...
    '0', all(strlength(refdes) > 0));
record('BOM duplicate reference-designator rows', ...
    height(bom) - numel(unique(refdes)), '0', ...
    numel(unique(refdes)) == height(bom));

require_bom(bom, 'Q1-Q8', 'AONS66917T', @record);
require_bom(bom, 'UDRV1-UDRV4', 'UCC21520DWR', @record);
require_bom(bom, 'UISO1-UISO4', 'UCC33421QDHARQ1', @record);
require_bom(bom, 'T1', '定制PQ4040 PC95', @record);
require_bom(bom, 'LCM1', '定制48mm双绕组磁环', @record);
require_bom(bom, 'LOUT', 'SER2915H-222KL', @record);
require_bom(bom, 'U_LATCH', 'SN74LVC1G74DCTR', @record);
require_bom(bom, 'UDIS1-4', 'SN74LVC1G10DBVR', @record);
require_bom(bom, 'U_OV_OD', 'SN74LVC2G07DBVR', @record);
require_bom(bom, 'U_ESTOP_SCH', 'SN74LVC1G17DBVR', @record);
require_bom(bom, 'FB_5V_SENSE', '600Ω@100MHz低DCR磁珠', @record);
require_bom(bom, 'C5V_SENSE_BULK', '22uF 10V X7R', @record);
require_bom(bom, 'C5V_SENSE_HF', '100nF 10V X7R', @record);

bomCells = fillmissing(string(table2cell(bom)), 'constant', "");
bomText = join(bomCells(:), newline);
forbiddenBom = ["LTRANSFER_LINK", "QFAST_DNP", ...
    "留铜排旁路", "SN74LVC1G74DCKR", "Isat大于100A"];
for k = 1:numel(forbiddenBom)
    hit = contains(bomText, forbiddenBom(k));
    record("Forbidden BOM text: " + forbiddenBom(k), hit, 'false', ~hit);
end
record('BOM contains 5V_SENSE AMC wiring', ...
    contains(bomText, '5V_SENSE的pin12到pin9'), 'true', ...
    contains(bomText, '5V_SENSE的pin12到pin9'));
record('BOM forbids LOUT bypass', ...
    contains(bomText, '禁止设置旁路铜排或0Ω短接位'), 'true', ...
    contains(bomText, '禁止设置旁路铜排或0Ω短接位'));
record('BOM locks 470pF snubber first fit', ...
    contains(bomText, '首板八路统一安装470pF'), 'true', ...
    contains(bomText, '首板八路统一安装470pF'));

%% Authoritative document consistency
docFiles = [
    "00_首板硬件定版索引与结论.md"
    "01_原理图逐页绘制指南_逐器件解释.md"
    "02_四层PCB布局布线与驱动小板规范.md"
    "04_首次上电与硬件验收清单.md"
    "05_关键参数补充_下单前必读.md"
    "07_关键选型计算与余量.md"
    "09_首板硬件闭环与逐元件最终说明.md"
    "11_完整原理图逐器件逐引脚接线表.md"
    "14_H1.7_PCB打板前硬件冻结报告.md"
    ];
docText = strings(size(docFiles));
for k = 1:numel(docFiles)
    docText(k) = string(fileread(fullfile(notesDir, docFiles(k))));
end
record('Authoritative documents mention 5V_SENSE', ...
    sum(contains(docText, '5V_SENSE')), sprintf('%d', numel(docFiles)), ...
    all(contains(docText, '5V_SENSE')));
stalePhrases = ["Isat>100A", "10% 电感下降电流约 100A", ...
    "保留 LOUT 铜排旁路", "LOUT 两端必须留低电感铜排旁路位"];
for k = 1:numel(stalePhrases)
    hit = any(contains(docText, stalePhrases(k)));
    record("Stale document phrase: " + stalePhrases(k), hit, ...
        'false', ~hit);
end

oldWorkbook1 = fullfile(notesDir, 'DAB_RA8P1_首板硬件定版BOM_H1.5.xlsx');
oldWorkbook2 = fullfile(notesDir, 'DAB_RA8P1_新版BOM与选型.xlsx');
record('Old H1.5 workbook removed', isfile(oldWorkbook1), 'false', ...
    ~isfile(oldWorkbook1));
record('Old pre-H1.7 workbook removed', isfile(oldWorkbook2), 'false', ...
    ~isfile(oldWorkbook2));

results = table(name, actual, expected, pass, ...
    'VariableNames', {'Check', 'Actual', 'Expected', 'Pass'});
disp(results);
outputDir = fullfile(notesDir, 'final_physical_models_screenshots');
writetable(results, fullfile(outputDir, ...
    'DAB_RA8P1_H1p7_Hardware_Freeze_Audit.csv'));
assert(all(results.Pass), 'H1.7 hardware freeze audit failed.');
fprintf('PASS: H1.7 model, BOM, and drawing-document freeze audit.\n');
end

function value = numeric_param(mdl, sid, parameter)
block = Simulink.ID.getFullName(mdl + ":" + sid);
value = str2double(get_param(block, parameter));
assert(isfinite(value), 'Non-numeric %s on SID %d.', parameter, sid);
end

function record_block_group(mdl, blockName, parameter, target, count, ...
        checkName, recorder)
blocks = find_system(mdl, 'LookUnderMasks', 'all', 'FollowLinks', 'on', ...
    'Type', 'Block', 'Name', blockName);
values = nan(numel(blocks), 1);
for k = 1:numel(blocks)
    values(k) = str2double(get_param(blocks{k}, parameter));
end
condition = numel(blocks) == count && ...
    all(abs(values - target) <= max(1e-12, abs(target) * 1e-12));
recorder(checkName, sprintf('%d blocks, values=%s', numel(blocks), ...
    mat2str(values.')), sprintf('%d blocks at %.12g', count, target), ...
    condition);
end

function require_bom(bom, refdes, part, recorder)
row = string(bom.("位号")) == refdes;
actualPart = "<missing>";
if nnz(row) == 1
    actualPart = string(bom.("首选型号")(row));
end
recorder("BOM " + refdes, actualPart, part, ...
    nnz(row) == 1 && actualPart == part);
end
