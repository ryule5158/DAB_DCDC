function export_final_mos_waveforms()
% Export final MOSFET model waveform evidence from model sensor signals.

rootDir = fileparts(mfilename('fullpath'));
if isempty(rootDir)
    rootDir = pwd;
end
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
outDir = fullfile(notesDir, 'final_physical_models_screenshots');
if ~exist(outDir, 'dir')
    mkdir(outDir);
end

mdl = "DAB_RA8P1_Final_Physical_MOSFET";
mdlPath = fullfile(notesDir, mdl + ".slx");
load_system(mdlPath);

cases = [
    24 48 100
    24 36 500
    ];
names = [
    "final_mos_48V_100W_waveforms"
    "final_mos_36V_500W_waveforms"
    ];

for k = 1:size(cases, 1)
    cfg.Vin = cases(k, 1);
    cfg.Vref = cases(k, 2);
    cfg.Ptarget = cases(k, 3);
    simOut = run_case(mdl, cfg);
    export_case_plot(simOut, mdl, cfg, fullfile(outDir, names(k) + ".png"));
end
end

function simOut = run_case(mdl, cfg)
rload = cfg.Vref^2 / cfg.Ptarget;
in = Simulink.SimulationInput(char(mdl));
in = in.setModelParameter('StopTime', '0.00045', ...
    'ReturnWorkspaceOutputs', 'on', 'SaveFormat', 'Dataset');
in = in.setBlockParameter(resolve_block_path(mdl, "VIN_18_32V_Bench_Source"), ...
    'dc_voltage', sprintf('%.10g', cfg.Vin));
in = in.setBlockParameter(char(mdl + "/MCU_Vout_Setpoint_V"), ...
    'Value', sprintf('%.10g', cfg.Vref));
in = in.setBlockParameter(resolve_block_path(mdl, "Rload_DC_Output"), ...
    'R', sprintf('%.10g', rload));

simOut = sim(in);
end

function export_case_plot(simOut, mdl, cfg, outPng)
[tV, vout] = get_signal(simOut, mdl, 'vout');
[tIin, iin] = get_signal(simOut, mdl, 'iin');
[tIout, iout] = get_signal(simOut, mdl, 'iout');
[tPin, pin] = get_signal(simOut, mdl, 'pin');
[tPout, pout] = get_signal(simOut, mdl, 'pout');
[tEff, eff] = get_signal(simOut, mdl, 'efficiency');
[tPh, ph] = get_signal(simOut, mdl, 'phase_us');

fig = figure('Visible', 'off', 'Color', 'w', 'Position', [120 120 1200 850]);
tl = tiledlayout(fig, 4, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
title(tl, sprintf('Final MOSFET DAB: Vin=%g V, Vref=%g V, Ptarget=%g W', ...
    cfg.Vin, cfg.Vref, cfg.Ptarget), 'FontWeight', 'bold');

nexttile;
plot(tV*1e6, vout, 'LineWidth', 1.2);
grid on;
yline(cfg.Vref, '--', 'Vref');
ylabel('Vout (V)');

nexttile;
plot(tIin*1e6, abs(iin), 'LineWidth', 1.1);
hold on;
plot(tIout*1e6, abs(iout), 'LineWidth', 1.1);
grid on;
ylabel('Current (A)');
legend('Iin abs', 'Iout abs', 'Location', 'best');

nexttile;
plot(tPin*1e6, pin, 'LineWidth', 1.1);
hold on;
plot(tPout*1e6, pout, 'LineWidth', 1.1);
grid on;
ylabel('Power (W)');
legend('Pin', 'Pout', 'Location', 'best');

nexttile;
yyaxis left;
plot(tEff*1e6, eff, 'LineWidth', 1.1);
ylabel('Efficiency (%)');
yyaxis right;
plot(tPh*1e6, ph, 'LineWidth', 1.1);
ylabel('Phase (us)');
grid on;
xlabel('Time (us)');

exportgraphics(fig, outPng, 'Resolution', 180);
close(fig);
fprintf('Exported %s\n', outPng);
end

function blk = resolve_block_path(mdl, blockName)
matches = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, 'Name', char(blockName));
if isempty(matches)
    error('Block "%s" was not found in model "%s".', blockName, mdl);
end
blk = matches{1};
end

function [t, y] = get_signal(simOut, mdl, suffix)
s = simOut.get(char(mdl + "_" + suffix));
if isa(s, 'timeseries')
    t = s.Time(:);
    y = s.Data(:);
elseif isstruct(s) && isfield(s, 'time') && isfield(s, 'signals')
    t = s.time(:);
    y = s.signals.values(:);
else
    error('Unsupported signal format for %s', suffix);
end
end
