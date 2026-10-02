disp(sum(bounded()));
disp(sum(bounded(0, 10, [0 10])));
disp(opened(1));
disp(lower_open(10));
disp(upper_open(0));
disp(two_flags(1));
disp(reversed_flags(1));

function output = bounded(lower, upper, values)
arguments
lower (1,1) double {mustBeFinite(lower)} = 0
upper (1,1) double {mustBeGreaterThan(upper,lower)} = 10
values (1,:) double {mustBeNumeric(values),mustBeFinite, ...
    mustBeInRange(values,lower,upper)} = lower + 1
end
arguments (Output)
output (1,:) double {mustBeInRange(output,lower,upper,'inclusive')}
end
output = values;
end

function output = opened(value)
arguments
value (1,1) double {mustBeInRange(value,0,10,"exclusive")}
end
output = value;
end

function output = lower_open(value)
arguments
value (1,1) double {mustBeInRange(value,0,10,'exclude-lower')}
end
output = value;
end

function output = upper_open(value)
arguments
value (1,1) double {mustBeInRange(value,0,10,"exclude-upper")}
end
output = value;
end

function output = two_flags(value)
arguments
value (1,1) double {mustBeInRange(value,0,10,'exclude-lower',"exclude-upper")}
end
output = value;
end

function output = reversed_flags(value)
arguments
value (1,1) double {mustBeInRange(value,0,10,'exclude-upper',"exclude-lower")}
end
output = value;
end
