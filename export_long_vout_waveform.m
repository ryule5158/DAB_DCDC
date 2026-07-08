function export_long_vout_waveform()
%EXPORT_LONG_VOUT_WAVEFORM Export 10 ms Vout evidence for the final model.

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
modelFile = fullfile(notesDir, mdl + ".slx");
if bdIsLoaded(char(mdl))
    close_system(char(mdl), 0);
end
load_system(modelFile);

cfg.Vin = 24;
cfg.Vref = 48;
cfg.Ptarget = 100;
out = run_case(mdl, cfg);
[t, vout] = get_signal(out, mdl, 'vout');

tailStart = 0.0050;
idxTail = t >= tailStart;
vMean = mean(vout(idxTail));
vP2P = max(vout(idxTail)) - min(vout(idxTail));

fig = figure('Visible', 'off', 'Color', 'w', 'Position', [120 120 1200 640]);
plot(t*1e3, vout, 'LineWidth', 1.2);
grid on;
yline(cfg.Vref, '--', '48 V reference', 'LabelHorizontalAlignment', 'left');
xline(tailStart*1e3, ':', 'metric window start');
xlabel('Time (ms)');
ylabel('Vout (V)');
title(sprintf('Final MOSFET DAB long Vout: Vin=%g V, Vref=%g V, P=%g W, tail mean=%.4f V, tail ripple=%.4f Vpp', ...
    cfg.Vin, cfg.Vref, cfg.Ptarget, vMean, vP2P), 'FontWeight', 'bold');
ylim([47.85 48.08]);
exportgraphics(fig, fullfile(outDir, 'final_mos_48V_100W_long_vout.png'), 'Resolution', 160);
close(fig);
fprintf('Exported %s\n', fullfile(outDir, 'final_mos_48V_100W_long_vout.png'));
end

function out = run_case(mdl, cfg)
rload = cfg.Vref^2 / cfg.Ptarget;
in = Simulink.SimulationInput(char(mdl));
in = in.setModelParameter('StopTime', '0.010');
in = in.setModelParameter('ReturnWorkspaceOutputs', 'on');
in = in.setModelParameter('SaveFormat', 'Dataset');
in = in.setBlockParameter(resolve_block_path(mdl, "VIN_18_32V_Bench_Source"), ...
    'dc_voltage', sprintf('%.10g', cfg.Vin));
in = in.setBlockParameter(char(mdl + "/MCU_Vout_Setpoint_V"), ...
    'Value', sprintf('%.10g', cfg.Vref));
in = in.setBlockParameter(resolve_block_path(mdl, "Rload_DC_Output"), ...
    'R', sprintf('%.10g', rload));
out = sim(in);
end

function blk = resolve_block_path(mdl, blockName)
matches = find_system(char(mdl), 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, 'Name', char(blockName));
if isempty(matches)
    error('Block "%s" was not found in model "%s".', blockName, mdl);
end
blk = matches{1};
end

function [t, y] = get_signal(out, mdl, suffix)
s = out.get(char(mdl + "_" + suffix));
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
