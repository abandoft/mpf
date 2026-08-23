function output = expand_scalar(value)
arguments (Input)
value (2,3) double {mustBeFinite}
end
arguments (Output)
output (2,3) double {mustBeFinite}
end
output = value;
end

function output = row_vector(value)
arguments (Input)
value (1,:) double {mustBeFinite}
end
arguments (Output)
output (1,:) double {mustBeFinite}
end
output = value;
end

function output = converted_default(value, offset)
arguments (Input)
value (1,1) logical
offset (1,1) double {mustBePositive} = value + 1
end
arguments (Output)
output (1,1) double {mustBePositive}
end
output = offset;
end

expanded = expand_scalar(4);
row = row_vector([1; 2; 3]);
disp(sum(expanded));
disp(row(1,2));
disp(converted_default(2));
