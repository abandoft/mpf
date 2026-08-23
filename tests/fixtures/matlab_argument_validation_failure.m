disp(require_positive(-1))

function output = require_positive(input)
arguments
input (1,1) double {mustBePositive}
end
output = input;
end
