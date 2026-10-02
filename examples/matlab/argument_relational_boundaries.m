disp(decimal_limit(10));
disp(integer_limit(1));
disp(rounding_limit(9007199254740992));
disp(sign_limit(-9));
disp(empty_limit([]));
disp(logical_limit(0, 1));
disp(upper_limit(1/0, 2));
disp(subnormal_limit(1));
disp(underflow_limit(0));

function output = decimal_limit(value)
arguments
value (1,1) double {mustBeLessThanOrEqual(value,010)}
end
output = 1;
end

function output = integer_limit(value)
arguments
value (1,1) double {mustBeLessThan(value,18446744073709551616)}
end
output = 1;
end

function output = rounding_limit(value)
arguments
value (1,1) double {mustBeGreaterThanOrEqual(value,9007199254740993)}
end
output = 1;
end

function output = sign_limit(value)
arguments
value (1,1) double {mustBeGreaterThan(value,- 10)}
end
output = 1;
end

function output = empty_limit(value)
arguments
value (1,:) double {mustBeGreaterThan(value,10)}
end
output = 1;
end

function output = logical_limit(lower, value)
arguments
lower (1,1) logical
value (1,1) double {mustBeGreaterThan(value,lower)}
end
output = 1;
end

function output = upper_limit(upper, value)
arguments
upper (1,1) double
value (1,1) double {mustBeLessThanOrEqual(value,upper)}
end
output = value;
end

function output = subnormal_limit(value)
arguments
value (1,1) double {mustBeGreaterThan(value,5e-324)}
end
output = 1;
end

function output = underflow_limit(value)
arguments
value (1,1) double {mustBeLessThanOrEqual(value,-1e-400)}
end
output = 1;
end
