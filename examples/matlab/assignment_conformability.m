matrix = [1 2 3; 4 5 6];

matrix(:, 1) = [7 8];
matrix(1, :) = [9; 10; 11];
matrix(:, 2) = [12];
linear = [1 2 3 4];
linear([1 2; 3 4]) = [13; 14; 15; 16];

disp(matrix(1, 1), matrix(2, 1), matrix(1, 2), ...
     matrix(2, 2), matrix(1, 3), matrix(2, 3), ...
     linear(1), linear(2), linear(3), linear(4))
