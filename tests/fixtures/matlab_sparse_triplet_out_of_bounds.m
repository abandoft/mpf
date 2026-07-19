row_indices = [1 4];
column_indices = [1 2];
stored_values = [2 3];
matrix = sparse(row_indices, column_indices, stored_values, 3, 3);
disp(nnz(matrix))
