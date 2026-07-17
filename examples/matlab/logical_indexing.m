values = [10 20 30 40];
mask = [true false true false];
selected = values(mask);
values(mask) = [1 2];
matrix = [1 2; 3 4];
matrix_mask = [true false; false true];
picked = matrix(matrix_mask);
disp(selected(1) + selected(2) + values(1) + values(3) + picked(1) + picked(2))
