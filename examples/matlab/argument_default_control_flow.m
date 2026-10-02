% Default evaluation is guarded by argument absence, including side effects and exceptions.
disp(checked());
disp(checked(5));
disp(checked(5, 1));
disp(lazy());
disp(lazy(0));
try
    checked([]);
catch
    disp(444);
end
try
    failing();
catch exception
    disp(exception.identifier);
end
disp(failing(2, 2));
disp(logical_checked([1, 2] > 1));
disp(logical_as_double([1, 2] > 1));
disp(logical_default());

function output = checked(first, second)
arguments
    first (1,1) double = seed()
    second (1,1) double = next(first)
end
output = first + second;
end

function output = seed()
disp(111);
output = 2;
end

function output = next(first)
arguments
    first (1,1) double
end
disp(222);
output = first + 3;
end

function output = lazy(value)
arguments
    value (1,1) logical = 0 || truth_seed()
end
output = value;
end

function output = truth_seed()
disp(333);
output = 1;
end

function output = failing(first, second)
arguments
    first (1,1) double = throw_seed()
    second (1,1) double = truth_seed()
end
output = first + second;
end

function output = throw_seed()
disp(555);
output = 2;
error('MPF:Default', 'default failed');
end

function output = logical_checked(values)
arguments
    values (1,2) logical {mustBeNumericOrLogical, mustBeFinite}
end
output = sum(values);
end

function output = logical_as_double(values)
arguments
    values (1,2) double {mustBeNumeric}
end
output = sum(values);
end

function output = logical_default(values)
arguments
    values (1,2) logical {mustBeNumericOrLogical, mustBeFinite} = [1, 2] > 1
end
output = sum(values);
end
