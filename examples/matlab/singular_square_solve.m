coefficient = [1 0; 0 0];
right_hand_side = [1; 1];
solution = coefficient \ right_hand_side;
right_solution = [1 1] / coefficient;

marker = 0;
if solution(2) > 1e300
    marker = 2;
end
if right_solution(2) > 1e300
    marker = marker + 4;
end
disp(solution(1) + right_solution(1) + marker)
