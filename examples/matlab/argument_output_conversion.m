disp(logical_result());
[flag, matrix] = multiple_results();
disp(flag);
disp(matrix(2,3));
disp(numel(matrix));
row = reshaped_result();
disp(row(1,4));
disp(numel(row));
disp(early_result(true));
disp(early_result(false));
disp(complex_logical());
value = complex_double();
disp(real(value));
disp(imag(value));
disp(shared_name(2));
values = size_free();
disp(values(2));
unknown = dynamic_result([1; 2; 3]);
disp(unknown(1,3));
disp(numel(unknown));
empty = empty_result();
disp(length(empty));
disp(numel(empty));

function output = logical_result()
arguments (Output)
output (1,1) logical
end
output = 2;
disp(output);
output = output + 1;
disp(output);
end

function [flag, matrix] = multiple_results()
arguments (Output)
flag (1,1) logical
matrix (2,3) double {mustBeFinite}
end
flag = 2;
matrix = 2;
disp(numel(matrix));
end

function output = reshaped_result()
arguments (Output)
output (1,:) double
end
output = [1 3; 2 4];
end

function output = early_result(stop)
arguments
stop (1,1) logical
end
arguments (Output)
output (1,1) logical
end
output = 2;
if stop
return
end
output = 0;
end

function output = complex_logical()
arguments (Output)
output (1,1) logical
end
output = 2i;
end

function output = complex_double()
arguments (Output)
output (1,1) double {mustBeFinite}
end
output = 2 + 3i;
end

function value = shared_name(value)
arguments
value (1,1) double
end
arguments (Output)
value (1,1) logical
end
end

function output = size_free()
arguments (Output)
output {mustBeNumeric}
end
output = [1 2];
end

function output = dynamic_result(value)
arguments (Output)
output (1,:) double
end
output = value;
end

function output = empty_result()
arguments (Output)
output (1,:) double
end
output = [];
end
