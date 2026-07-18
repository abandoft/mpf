diagonal = [2 0 0; 0 4 0; 0 0 5];
lower = [2 0 0; 1 3 0; 4 -2 5];
upper = [2 1 4; 0 3 -2; 0 0 5];

diagonal_left = diagonal \ [4; 8; 15];
lower_left = lower \ [2; 7; 15];
upper_left = upper \ [16; 0; 15];
diagonal_right = [4 8 15] / diagonal;
lower_right = [16 0 15] / lower;
upper_right = [2 7 15] / upper;
dense_left = [4 1; 2 3] \ [9; 8];

disp(diagonal_left(3), lower_left(2), upper_left(1), diagonal_right(1), ...
     lower_right(3), upper_right(2), dense_left(1))
