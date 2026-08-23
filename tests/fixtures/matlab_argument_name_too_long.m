require_name('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ab')

function output = require_name(input)
arguments
input (1,:) char {mustBeValidVariableName}
end
output = input;
end
