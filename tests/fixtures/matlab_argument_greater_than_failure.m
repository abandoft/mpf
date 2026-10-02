result = validate_value(5);
disp(result);

function output = validate_value(value)
arguments
value (1,1) double {mustBeGreaterThan(value,5)}
end
output = value;
end
