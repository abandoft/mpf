coefficient = [1 2; 2 4; 3 6];
right_hand_side = [1; 2; 3];
basic_solution = coefficient \ right_hand_side;
disp(round(2 * basic_solution(1)), round(2 * basic_solution(2)))
