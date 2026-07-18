positive_infinity = 1 / 0;
negative_infinity = 1 / -0.0;
not_a_number = 0 / 0;

disp((positive_infinity > 1e308) + (negative_infinity < -1e308) + ...
     (not_a_number ~= not_a_number))
