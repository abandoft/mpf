matrix = [1 2 3; 4 5 6];
row = matrix(2, :);
column = matrix(:, 2);
block = matrix(:, 1:2);
linear = matrix(:);
odd = matrix(1, 1:2:3);
disp(sum(row) + sum(column) + numel(block) + linear(2) + sum(odd))
