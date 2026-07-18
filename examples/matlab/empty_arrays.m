empty = [];
disp(length(empty), numel(empty))

grown = [];
grown(3) = 7;
disp(length(grown), grown(3))

matrix = reshape([], 0, 5);
disp(length(matrix), numel(matrix))

transposed = matrix.';
disp(length(transposed), numel(transposed))

scaled = matrix + 2;
disp(length(scaled), numel(scaled))

selected = matrix([], :);
disp(length(selected), numel(selected))

expanded = reshape([], 0, 4);
expanded(1, 2) = 9;
disp(length(expanded), expanded(1, 2))
