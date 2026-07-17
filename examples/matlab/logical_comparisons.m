values = [10 20 30 40];
selected = values(values >= 30);
values(values < 25) = 0;
matrix = [1 2 3; 4 5 6];
picked = matrix(matrix > [2 2 2]);
boolean_grid = [true false] == [true; false];
reshaped_mask = reshape(matrix >= 3, 6, 1);
disp(sum(selected) + sum(values) + sum(picked) + numel(boolean_grid(boolean_grid)) + numel(reshaped_mask(reshaped_mask)))
