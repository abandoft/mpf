result = validate_value(4);
disp(result);

function output = validate_value(value)
arguments
value (1,1) double {mustBeGreaterThanOrEqual(value,5)}
end
output = value;
end
