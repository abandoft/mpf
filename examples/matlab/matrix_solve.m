coefficient = [4 1; 2 3];
right_hand_side = [9; 8];
solution = coefficient \ right_hand_side;
multiple_solutions = coefficient \ [9 1; 8 2];
right_solution = [5 7] / coefficient;
right_matrix = [5 7; 1 2] / coefficient;
squared = coefficient ^ 2;
identity = coefficient ^ 0;
inverse = coefficient ^ -1;

disp(solution(1) + solution(2) + right_solution(1) + right_solution(2) + ...
     squared(1, 1) + squared(2, 2) + ...
     inverse(1, 1) + inverse(2, 2))
