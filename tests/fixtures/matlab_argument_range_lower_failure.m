disp(checked(0));
function output = checked(value)
arguments
value (1,1) double {mustBeInRange(value,0,10,'exclude-lower')}
end
output = value;
end
