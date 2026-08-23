values = [1 2 3];
linear_growth = replace_linear(values, [2 5], [8 9]);
linear_overwrite = replace_linear(values, [1 3], [7 6]);

matrix = [1 2 3; 4 5 6];
removed_column = remove_columns(matrix, 2);
removed_columns = remove_columns(matrix, [1 3]);

cube = reshape([1 2 3 4 5 6 7 8], 2, 2, 2);
grown_cube = replace_page(cube, 3, [9 11; 10 12]);
unchanged_cube = reject_page(cube, 3, [1 2 3]);

disp(linear_growth(1), linear_growth(2), linear_growth(3), ...
     linear_growth(4), linear_growth(5), ...
     linear_overwrite(1), linear_overwrite(2), linear_overwrite(3), ...
     removed_column(1, 1), removed_column(2, 1), ...
     removed_column(1, 2), removed_column(2, 2), ...
     removed_columns(1, 1), removed_columns(2, 1), ...
     grown_cube(1, 1, 1), grown_cube(2, 2, 2), ...
     grown_cube(1, 1, 3), grown_cube(2, 1, 3), ...
     grown_cube(1, 2, 3), grown_cube(2, 2, 3), ...
     unchanged_cube(1, 1, 1), unchanged_cube(2, 2, 2))

function result = replace_linear(input, indices, replacement)
  input(indices) = replacement;
  result = input;
end

function result = remove_columns(input, columns)
  input(:, columns) = [];
  result = input;
end

function result = replace_page(input, page, replacement)
  input(:, :, page) = replacement;
  result = input;
end

function result = reject_page(input, page, replacement)
  try
    input(:, :, page) = replacement;
  catch
  end
  result = input;
end
