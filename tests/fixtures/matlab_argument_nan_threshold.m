disp(validate_value(0/0, 2));

function output = validate_value(lower, value)
arguments
lower (1,1) double
value (1,1) double {mustBeGreaterThan(value,lower)}
end
output = value;
end
