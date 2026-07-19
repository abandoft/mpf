zero_matrix = sparse(3, 4);
empty_triplets = sparse([], [], [], 3, 4);
cancelled = sparse([1 3 1 2], [1 1 1 3], [2 4 -2 5], 3, 4, 8);
inferred = sparse([2 1 2], [3 2 3], [4 5 1]);
row_broadcast = sparse(2, [1 2 3], [7 0 -7], 3, 3);
column_broadcast = sparse([1 2 3], 2, [1 0 3], 3, 3);
value_broadcast = sparse([1 2], [2 3], 5, 3, 3);

transposed = cancelled.';
conjugate_transposed = inferred';
dense_transposed = full(transposed);
dense_conjugate_transposed = full(conjugate_transposed);

disp(nnz(zero_matrix), nnz(empty_triplets), nnz(cancelled), nnz(inferred), nnz(row_broadcast), ...
     nnz(column_broadcast), nnz(value_broadcast), 0 + issparse(transposed), ...
     0 + issparse(conjugate_transposed), dense_transposed(1, 3), ...
     dense_transposed(3, 2), dense_conjugate_transposed(2, 1), ...
     dense_conjugate_transposed(3, 2))
