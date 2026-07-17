left = [1 2; 3 4];
right = [5 6; 7 8];
product = left * right;
scaled = left .* 2;
quotient = right ./ [5 3; 7 2];
powered = [2 3] .^ [3 2];
left_divided = 2 .\ [4 6];
shifted = left + 1;
disp(product(1, 1) + product(2, 2) + scaled(2, 1) + quotient(2, 2) + ...
     powered(1) + powered(2) + left_divided(2) + shifted(1, 1))
