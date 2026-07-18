tridiagonal = [0 2 0; 1 3 4; 0 5 6];
tridiagonal_solution = tridiagonal \ [4; 19; 28];
tridiagonal_right = [2 23 26] / tridiagonal;

positive_definite = [4 2 2; 2 10 -2; 2 -2 6];
positive_definite_left = positive_definite \ [14; 16; 16];
positive_definite_right = [14 16 16] / positive_definite;

symmetric_indefinite = [0 1 2; 1 0 3; 2 3 0];
indefinite_solution = symmetric_indefinite \ [8; 10; 8];

disp(tridiagonal_solution(1), tridiagonal_solution(2), tridiagonal_solution(3), ...
     tridiagonal_right(1), tridiagonal_right(2), tridiagonal_right(3), ...
     positive_definite_left(1), positive_definite_left(2), positive_definite_left(3), ...
     positive_definite_right(1), positive_definite_right(2), positive_definite_right(3), ...
     indefinite_solution(1), indefinite_solution(2), indefinite_solution(3))
