scaled = scale([1.0 2.0 3.0]);
disp(sum(scaled))
disp(combine(20))
disp(negate(0 == 1) + 0)
disp(echo('ready'))
disp(validate_logical(0 == 0) + 0)
disp(validate_char('ok'))
disp(validate_empty(''))

function output = scale(values, factor)
arguments (Input)
values (1,:) double {mustBeNumeric, mustBeFinite}
factor (1,1) double {mustBePositive} = 2
end
arguments (Output)
output (1,:) double {mustBeFinite}
end
output = values .* factor;
end

function output = combine(left, right)
arguments
left (1,1) double {mustBeFinite}
right (1,1) double {mustBePositive} = left + 2
end
output = left + right;
end

function output = negate(input)
arguments
input (1,1) logical
end
arguments (Output)
output (1,1) logical
end
output = ~input;
end

function output = echo(input)
arguments
input (1,:) char {mustBeNonempty, mustBeRow, mustBeText, mustBeValidVariableName}
end
arguments (Output)
output (1,:) char {mustBeNonempty, mustBeTextScalar}
end
output = input;
end

function output = validate_logical(input)
arguments
input (1,1) logical {mustBeNumericOrLogical, mustBeReal, mustBeFinite, mustBeNonNan, mustBePositive, mustBeNonzero, mustBeInteger}
end
output = input;
end

function output = validate_char(input)
arguments
input (1,:) char {mustBeReal, mustBeFinite, mustBeNonNan, mustBeNonmissing}
end
output = length(input);
end

function output = validate_empty(input)
arguments
input (1,:) char {mustBeNumeric, mustBeFloat, mustBePositive, mustBeInteger}
end
output = length(input);
end
