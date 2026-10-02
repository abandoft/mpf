disp(validate_value(complex(1, 0), 1/0));

function output = validate_value(lower, value)
arguments
lower (1,1)
value (1,1) double {mustBeFinite,mustBeGreaterThan(value,lower)}
end
output = value;
end
