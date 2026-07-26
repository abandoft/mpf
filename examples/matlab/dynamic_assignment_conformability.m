values = [1 2 3 4];
matrix = [1 2; 3 4];

linear_result = replace_linear(values, [9; 8; 7; 6]);
scalar_result = replace_all(matrix, 5);
unchanged_result = reject_nonconforming(matrix, [9 8 7 6]);

disp(linear_result(1), linear_result(4), ...
     scalar_result(1, 1), scalar_result(2, 2), ...
     unchanged_result(1, 1), unchanged_result(2, 1), ...
     unchanged_result(1, 2), unchanged_result(2, 2))

function result = replace_linear(input, replacement)
  input(:) = replacement;
  result = input;
end

function result = replace_all(input, replacement)
  input(:, :) = replacement;
  result = input;
end

function result = reject_nonconforming(input, replacement)
  try
    input(:, :) = replacement;
  catch
  end
  result = input;
end
