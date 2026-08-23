problem = MException('MPF:Bad', 'message');
detail = identity('verbose');
disp(getReport(problem, detail))

function result = identity(value)
    result = value;
end
