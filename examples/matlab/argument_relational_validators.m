disp(bounded(-2, 6, [-2 0 5]));
disp(ordered_defaults());

function output = bounded(lower, upper, values)
arguments (Input)
lower (1,1) double {mustBeGreaterThan(lower,-10)}
upper (1,1) double {mustBeGreaterThan(upper,lower), mustBeLessThanOrEqual(upper,10)}
values (1,:) double {mustBeGreaterThanOrEqual(values,lower), mustBeLessThan(values,upper)}
end
arguments (Output)
output (1,1) double {mustBeLessThanOrEqual(output,upper)}
end
output = values(1) + values(2) + values(3);
end

function output = ordered_defaults(minimum, value)
arguments
minimum (1,1) double {mustBeGreaterThan(minimum,-10)} = -1
value (1,1) double {mustBeGreaterThan(value,minimum)} = minimum + 3
end
output = value;
end
