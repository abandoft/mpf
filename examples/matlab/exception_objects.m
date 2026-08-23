cause = MException('MPF:Cause', 'Cause %d', 3);
problem = MException('MPF:Bad', 'Value %+05d', 7);
problem = addCause(problem, cause);
disp(getReport(problem, 'extended', 'hyperlinks', 'off'))

formats = MException('MPF:Formats', ...
    'hex=%#06x fixed=%08.2f exp=%.3e general=%.4g alternate=%#.4g char=%c text=%.3s percent=%%', ...
    42, 3.5, 12.5, 12.5, 12.5, 65, 'hello');
disp(formats.message)

try
    throwAsCaller(problem)
catch caught
    disp(caught.identifier)
    disp(caught.message)
    disp(getReport(caught, 'extended', 'hyperlinks', 'off'))
end

try
    error('MPF:Formatted', 'Error %04d', 9)
catch formatted
    disp(formatted.identifier)
    disp(formatted.message)
end

ping

function ping
    disp('pong')
end
