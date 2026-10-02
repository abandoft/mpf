disp(checked(10));
function output = checked(value)
arguments
value (1,1) double {mustBeInRange(value,0,10,"exclude-upper")}
end
output = value;
end
