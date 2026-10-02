% Defaults resolve local function calls, even when their definitions occur later in the file.
% The printed markers also show that supplied arguments do not evaluate their defaults.
disp(checked());
disp(checked(4));
disp(checked(4, 9));

function output = checked(value, scale)
arguments
    value (1,1) double {mustBePositive} = seed()
    scale (1,1) double {mustBeGreaterThan(scale,value)} = scale_seed(value)
end
output = value + scale;
end

function output = seed()
disp(111);
output = leaf();
end

function output = scale_seed(value)
arguments
    value (1,1) double
end
disp(222);
output = value + 3;
end

function output = leaf()
output = 2;
end
