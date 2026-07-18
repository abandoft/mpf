coefficient = [1+1i 2+2i; 0 0; 1-1i 2-2i];
expected = [0; 1+1i];
right_hand_side = coefficient * expected;
basic_solution = coefficient \ right_hand_side;

disp(round(abs(basic_solution(1))), ...
     round(real(basic_solution(2))), round(imag(basic_solution(2))))
