matrix = [1 2; 3 4];
replacement = [9 8 7];
result = replace_column(matrix, 3, replacement);
disp(result(1, 1))

function result = replace_column(input, column, replacement)
  input(:, column) = replacement;
  result = input;
end
