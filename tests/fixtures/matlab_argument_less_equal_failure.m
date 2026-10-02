result = validate_value(6);
disp(result);

function output = validate_value(value)
arguments
value (1,1) double {mustBeLessThanOrEqual(value,5)}
end
output = value;
end
