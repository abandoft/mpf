disp(validate_value(complex(1, 0)));

function output = validate_value(value)
arguments
value (1,1) {mustBeGreaterThan(value,0)}
end
output = value;
end
