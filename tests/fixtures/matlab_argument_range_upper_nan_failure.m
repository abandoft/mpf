disp(checked(0/0,1));
function output = checked(upper,value)
arguments
upper (1,1) double
value (1,1) double {mustBeInRange(value,0,upper)}
end
output = value;
end
