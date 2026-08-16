function inspect_dab_control_model()
%INSPECT_DAB_CONTROL_MODEL Print controller and operating-point evidence.

rootDir = fileparts(fileparts(mfilename('fullpath')));
notesDir = fullfile(rootDir, 'DAB_RA8P1_PCB_Notes');
modelName = "DAB_RA8P1_Final_Physical_MOSFET";
modelFile = fullfile(notesDir, modelName + ".slx");

if bdIsLoaded(modelName)
    close_system(modelName, 0);
end
load_system(modelFile);
cleanup = onCleanup(@() close_if_loaded(modelName));

fprintf('MODEL=%s\n', modelFile);
fprintf('STOP_TIME=%s MAX_STEP=%s SOLVER=%s\n', ...
    get_param(modelName, 'StopTime'), get_param(modelName, 'MaxStep'), ...
    get_param(modelName, 'Solver'));

keywords = ["controller", "phase", "sps", "eps", "tps", "pwm", ...
    "vin", "vout", "load", "transformer", "leak"];
blocks = find_system(modelName, 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'Type', 'Block');
for k = 1:numel(blocks)
    blockName = string(get_param(blocks{k}, 'Name'));
    if any(contains(lower(blockName), keywords))
        fprintf('BLOCK SID=%s TYPE=%s PATH=%s\n', ...
            Simulink.ID.getSID(blocks{k}), get_param(blocks{k}, 'BlockType'), ...
            strrep(blocks{k}, newline, '<NL>'));
        print_selected_parameters(blocks{k});
    end
end

controller = find_system(modelName, 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', 'SearchDepth', inf, ...
    'Name', 'RA8P1_Realtime_Voltage_PI');
if ~isempty(controller)
    fprintf('CONTROLLER_BEGIN\n');
    controllerBlocks = find_system(controller{1}, 'LookUnderMasks', 'all', ...
        'FollowLinks', 'on', 'Type', 'Block');
    for k = 1:numel(controllerBlocks)
        fprintf('CTRL_BLOCK SID=%s TYPE=%s PATH=%s\n', ...
            Simulink.ID.getSID(controllerBlocks{k}), ...
            get_param(controllerBlocks{k}, 'BlockType'), ...
            strrep(controllerBlocks{k}, newline, '<NL>'));
        print_selected_parameters(controllerBlocks{k});
    end
    fprintf('CONTROLLER_END\n');
end

root = sfroot;
charts = root.find('-isa', 'Stateflow.Chart');
for k = 1:numel(charts)
    if ~startsWith(string(charts(k).Path), modelName)
        continue;
    end
    fprintf('CHART PATH=%s\n', charts(k).Path);
    states = charts(k).find('-isa', 'Stateflow.State');
    for n = 1:numel(states)
        fprintf('STATE=%s LABEL=%s\n', states(n).Name, ...
            strrep(states(n).LabelString, newline, '<NL>'));
    end
    transitions = charts(k).find('-isa', 'Stateflow.Transition');
    for n = 1:numel(transitions)
        fprintf('TRANSITION=%s\n', ...
            strrep(transitions(n).LabelString, newline, '<NL>'));
    end
end

emCharts = root.find('-isa', 'Stateflow.EMChart');
for k = 1:numel(emCharts)
    if ~startsWith(string(emCharts(k).Path), modelName)
        continue;
    end
    fprintf('EMCHART PATH=%s\nSCRIPT_BEGIN\n%s\nSCRIPT_END\n', ...
        emCharts(k).Path, emCharts(k).Script);
end
end

function print_selected_parameters(block)
parameterNames = ["Value", "Gain", "R", "L", "l", "dc_voltage", ...
    "MaskType", "FunctionName"];
dialogParameters = get_param(block, 'DialogParameters');
if isempty(dialogParameters)
    return;
end
for parameterName = parameterNames
    if isfield(dialogParameters, parameterName)
        try
            value = string(get_param(block, parameterName));
            fprintf('  PARAM %s=%s\n', parameterName, ...
                strrep(value, newline, '<NL>'));
        catch
        end
    end
end
end

function close_if_loaded(modelName)
if bdIsLoaded(modelName)
    close_system(modelName, 0);
end
end
