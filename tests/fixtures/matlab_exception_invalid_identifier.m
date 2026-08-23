identifier = identity('invalid');
problem = MException(identifier, 'message');
disp(problem.message)

function result = identity(value)
    result = value;
end
