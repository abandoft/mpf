matrix = [1 2 3; 4 5 6];
plain = matrix.';
conjugating = matrix';
row = [7 8];
column = row';
disp(plain(3, 2) + conjugating(2, 1) + column(2, 1))
