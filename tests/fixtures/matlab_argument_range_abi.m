disp(checked(1));
function output = checked(value)
arguments
value (1,1) double {mustBeInRange(value,0,10)}
end
output = value;
end
