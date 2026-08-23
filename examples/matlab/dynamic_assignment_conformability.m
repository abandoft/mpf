values = [1 2 3 4];
matrix = [1 2; 3 4];

linear_result = replace_linear(values, [9; 8; 7; 6]);
scalar_result = replace_all(matrix, 5);
unchanged_result = reject_nonconforming(matrix, [9 8 7 6]);
overwritten_column = replace_column(matrix, 2, [8; 9]);
grown_column = replace_column(matrix, 3, [6; 7]);
scalar_column = replace_column(matrix, 4, 5);
numeric_columns = replace_column(matrix, [2 4], [8 10; 9 11]);
logical_columns = replace_column(matrix, [true false], [18; 19]);
ranged_columns = replace_range(matrix, 2, 4, [12 14 16; 13 15 17]);
unchanged_growth = reject_nonconforming_growth(matrix, 3, [1 2 3]);

disp(linear_result(1), linear_result(4), ...
     scalar_result(1, 1), scalar_result(2, 2), ...
     unchanged_result(1, 1), unchanged_result(2, 1), ...
     unchanged_result(1, 2), unchanged_result(2, 2), ...
     overwritten_column(1, 2), overwritten_column(2, 2), ...
     grown_column(1, 3), grown_column(2, 3), ...
     scalar_column(1, 4), scalar_column(2, 4), ...
     numeric_columns(1, 2), numeric_columns(2, 2), ...
     numeric_columns(1, 3), numeric_columns(2, 3), ...
     numeric_columns(1, 4), numeric_columns(2, 4), ...
     logical_columns(1, 1), logical_columns(2, 1), ...
     logical_columns(1, 2), logical_columns(2, 2), ...
     ranged_columns(1, 2), ranged_columns(2, 2), ...
     ranged_columns(1, 3), ranged_columns(2, 3), ...
     ranged_columns(1, 4), ranged_columns(2, 4), ...
     unchanged_growth(1, 1), unchanged_growth(2, 1), ...
     unchanged_growth(1, 2), unchanged_growth(2, 2))

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

function result = replace_column(input, column, replacement)
  input(:, column) = replacement;
  result = input;
end

function result = reject_nonconforming_growth(input, column, replacement)
  try
    input(:, column) = replacement;
  catch
  end
  result = input;
end

function result = replace_range(input, first, last, replacement)
  input(:, first:last) = replacement;
  result = input;
end
