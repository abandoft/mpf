matrix = [10 20 30; 40 50 60];
row = [1 2 3];
column = [100; 200];
matrix_result = matrix + row;
outer_result = column + row;
cube = reshape([1 2 3 4 5 6], 2, 1, 3);
pages = reshape([10 20 30], 1, 1, 3);
cube_result = cube + pages;
explicit_row = reshape([1 2 3], 1, 3);
row_equivalent = row + explicit_row;
matrix_pages = reshape([1 2 3 4 5 6], 2, 3);
singleton_cube = reshape([10 11 12 13 14 15], 2, 3, 1);
rank_equivalent = matrix_pages + singleton_cube;
disp(matrix_result(2, 3) + outer_result(2, 3) + cube_result(2, 1, 3) + row_equivalent(1, 3) + rank_equivalent(2, 3, 1))
