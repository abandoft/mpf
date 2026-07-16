matrix = [1 2 3; 4 5 6];
matrix(:) = [1 4 2 5 3 6];
matrix(:, 1:2) = [1 2; 3 4];
matrix(:, 2) = [8; 9];
matrix(:, 3) = 7;
matrix(1, :) = [4 5 6];
disp(matrix(1,1) + matrix(2,2) + matrix(1,3))
