row = [1 2 0];
matrix = [1 0 3; 4 5 6];
cube = reshape([1 0 3 4 5 6 0 8], 2, 2, 2);

column_all = all(matrix);
row_any = any(matrix, 2);
page_any = any(cube, [1 2]);

empty_rows = reshape([], 0, 3);
empty_columns = reshape([], 3, 0);
empty_all = all(empty_rows);
empty_any = any(empty_columns);

beyond_rank = all(row, 3);
no_dimensions = any(matrix, []);

disp(all(row) + 2 * any(row))
disp(column_all(1, 1) + 2 * column_all(1, 2) + 4 * column_all(1, 3))
disp(row_any(1, 1) + 2 * row_any(2, 1))
disp(page_any(1, 1, 1) + 2 * page_any(1, 1, 2))
disp(empty_all(1, 1) + empty_all(1, 2) + empty_all(1, 3) + numel(empty_any))
disp(all(matrix, 'all') + 2 * any(matrix, 'all') + 4 * all([]) + 8 * any([]))
disp(beyond_rank(1) + 2 * beyond_rank(2) + 4 * beyond_rank(3))
disp(no_dimensions(1, 1) + 2 * no_dimensions(1, 2) + 4 * no_dimensions(2, 2))
