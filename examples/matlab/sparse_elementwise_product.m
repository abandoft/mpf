A = sparse([1 0 2; 0 3 0]);
D = [10 20 30; 40 50 60];
row = sparse([1 1], [1 3], [2 4], 1, 3);
column = sparse([1 2], [1 1], [5 6], 2, 1);

right_scalar = A .* 2;
left_scalar = 3 .* A;
sparse_dense = A .* D;
dense_sparse = D .* A;
sparse_sparse = A .* A;
row_dense = row .* D;
dense_column = D .* column;
outer_sparse = row .* column;
zero_sparse = A .* 0;

right_full = full(right_scalar);
left_full = full(left_scalar);
sparse_dense_full = full(sparse_dense);
dense_sparse_full = full(dense_sparse);
sparse_sparse_full = full(sparse_sparse);
row_dense_full = full(row_dense);
dense_column_full = full(dense_column);
outer_sparse_full = full(outer_sparse);

disp(right_full(1, 3), left_full(2, 2), sparse_dense_full(2, 2), ...
     dense_sparse_full(2, 2), sparse_sparse_full(2, 2), row_dense_full(2, 3), ...
     dense_column_full(2, 2), outer_sparse_full(2, 3), nnz(zero_sparse), ...
     issparse(right_scalar) + 0, issparse(sparse_dense) + 0, ...
     issparse(dense_sparse) + 0, issparse(sparse_sparse) + 0, ...
     issparse(outer_sparse) + 0)
