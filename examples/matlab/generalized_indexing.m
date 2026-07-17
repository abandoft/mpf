values = [10 20 30 40];
picked = values([4 2 2]);
values([1 1 3]) = [5 6 7];
empty = values([]);

matrix = [1 2 3; 4 5 6; 7 8 9];
rows = [true false true];
columns = [false true true];
block = matrix(rows, columns);
matrix(rows, [3 1]) = [30 10; 90 70];

mask = values > 6;
selected = values(mask);
disp(picked(1), picked(2), picked(3), length(empty), block(2, 2), ...
     matrix(1, 1), matrix(3, 3), selected(2))
