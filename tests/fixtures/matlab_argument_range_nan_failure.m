disp(checked(0/0));
function output = checked(value)
arguments
value (1,1) double {mustBeInRange(value,0,10)}
end
output = value;
end
