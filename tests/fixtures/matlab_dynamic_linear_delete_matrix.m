matrix = [1 2; 3 4];
result = erase_linear(matrix, 2);
disp(result)

function output = erase_linear(values, position)
values(position) = [];
output = values;
end
