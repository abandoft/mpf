format = identity('Value %d');
problem = MException('MPF:Bad', format);
disp(problem.message)

function result = identity(value)
    result = value;
end
