matrix = [1 2; 3 4];
replacement = [1 2 3 4];
result = replace_dynamic(matrix, replacement);
disp(result(1, 1))

function result = replace_dynamic(values, replacement)
  values(:, :) = replacement;
  result = values;
end
