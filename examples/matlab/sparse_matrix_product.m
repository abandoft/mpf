A = sparse([1 0 2; 0 3 0]);
B = sparse([0 4; 5 0; 0 6]);
sparse_product = A * B;
sparse_dense_product = A * full(B);
dense_sparse_product = full(A) * B;
dense_product = full(sparse_product);
disp(dense_product(1, 2), dense_product(2, 1), ...
     sparse_dense_product(1, 2), dense_sparse_product(2, 1), ...
     0 + issparse(sparse_product), 0 + issparse(sparse_dense_product), ...
     0 + issparse(dense_sparse_product), nnz(sparse_product))
