disp(complex_interval(complex(0,9), complex(2,-9), complex(1,20)));
disp(logical_interval(0, 1, 1));
disp(empty_interval(0, 1, []));
disp(scalar_interval(-1/0, 1/0, 42));
disp(equal_interval(2));

function output = complex_interval(lower, upper, value)
arguments
lower (1,1)
upper (1,1)
value (1,1) {mustBeInRange(value,lower,upper)}
end
output = 1;
end

function output = logical_interval(lower, upper, value)
arguments
lower (1,1) logical
upper (1,1) logical
value (1,1) logical {mustBeInRange(value,lower,upper)}
end
output = 0 + value;
end

function output = empty_interval(lower, upper, value)
arguments
lower (1,1) double
upper (1,1) double
value (1,:) double {mustBeInRange(value,lower,upper,'exclusive')}
end
output = length(value);
end

function output = scalar_interval(lower, upper, value)
arguments
lower (1,1) double
upper (1,1) double
value (1,1) double {mustBeInRange(value,lower,upper)}
end
output = value;
end

function output = equal_interval(value)
arguments
value (1,1) double {mustBeInRange(value,2,2)}
end
output = value;
end
