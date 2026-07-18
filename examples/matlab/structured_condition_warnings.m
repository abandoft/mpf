tiny = 0.00000000000000001;
diagonal = [1 0; 0 tiny];
upper = [1 1; 0 tiny];

diagonal_left = diagonal \ [1; tiny];
upper_left = upper \ [2; tiny];
upper_right = [0 tiny] / upper;

disp(diagonal_left(2) + upper_left(1) + upper_right(2))
