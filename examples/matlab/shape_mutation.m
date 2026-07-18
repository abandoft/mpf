row = [1 2 3];
row(5) = 9;

matrix = [1 2; 3 4];
matrix(3, 3) = 8;

linear = [1 2; 3 4];
linear(7) = 6;

removed = [10 20 30 40];
removed([2 4]) = [];

columns = [1 2 3; 4 5 6];
columns(:, 2) = [];

cube = reshape([1 2 3 4 5 6 7 8], 2, 2, 2);
cube(3, 2, 2) = 9;
cube(:, 1, :) = [];

disp(row(4), row(5), matrix(3, 3), linear(1, 4), removed(2), columns(2, 2), ...
     cube(3, 1, 2), cube(1, 1, 1))

dynamic_grown = grow_at([1 2 3], 5);
dynamic_trimmed = erase_at([10 20 30 40], 2);
disp(dynamic_grown(4), dynamic_grown(5), dynamic_grown(6), dynamic_trimmed(2))

function output = grow_at(values, position)
values(position) = 9;
values(end + 1) = 7;
output = values;
end

function output = erase_at(values, position)
values(position) = [];
output = values;
end
