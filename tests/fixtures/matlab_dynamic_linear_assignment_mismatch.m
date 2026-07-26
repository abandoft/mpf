values = [1 2 3 4];
replacement = [9 8 7];
result = replace_linear(values, replacement);
disp(result(1))

function result = replace_linear(input, replacement)
  input(:) = replacement;
  result = input;
end
