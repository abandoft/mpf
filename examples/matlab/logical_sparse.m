dense = sparse([true false; false true]);
duplicates = sparse([1 1 2 2], [1 1 2 2], [false true false true], 2, 2);
transposed = dense.';
selected = duplicates([2 1], [2 1]);
reshaped = reshape(selected, [1 4]);
duplicates(1, 2) = true;
duplicates(2, 2) = false;
dense_full = full(dense);
result_full = full(reshaped);
disp(nnz(dense), nnz(duplicates), nnz(transposed), nnz(selected), nnz(reshaped), ...
     nnz(dense_full), nnz(result_full))
