coefficient = [16 2 3 13; 5 11 10 8; 9 7 6 12; 4 14 15 1];
right_hand_side = [34; 34; 34; 34];
solution = coefficient \ right_hand_side;

disp(round(solution(1) + solution(2) + solution(3) + solution(4)))
