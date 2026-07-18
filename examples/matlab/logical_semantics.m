row = [1 0 3];
column = [1; 0];
both = row & column;
either = row | column;
inverse = ~row;

cube = reshape([1 0 2 0], 2, 1, 2);
pages = reshape([0 1], 1, 1, 2);
tensor_mask = cube | pages;

full_condition = 0;
if [1 2; 3 4]
  full_condition = 10;
end

zero_condition = 0;
if [1 0; 3 4]
  zero_condition = 10;
end

empty_condition = 10;
if []
  empty_condition = 99;
end

short_condition = 0;
if 0 && (1 / 0)
  short_condition = 10;
end
if 1 || (1 / 0)
  short_condition = short_condition + 20;
end

scalar_and = 0;
if 1 & 2
  scalar_and = 1;
end
scalar_or = 0;
if 0 | 3
  scalar_or = 1;
end

disp(numel(both(both)))
disp(numel(either(either)))
disp(numel(inverse(inverse)))
disp(numel(tensor_mask(tensor_mask)))
disp(full_condition)
disp(zero_condition)
disp(empty_condition)
disp(short_condition)
disp(scalar_and)
disp(scalar_or)
