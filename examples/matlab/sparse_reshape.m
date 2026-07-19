A = sparse([1 0 2; 0 3 0]);
vector = reshape(A, [6 1]);
inferred_rows = reshape(A, [], 3);
inferred_columns = reshape(A, 2, []);
folded = reshape(A, 1, 2, 3);
restored = reshape(folded, [2 3]);
disp(vector(4), vector(5), inferred_rows(2,2), inferred_columns(1,3), ...
     folded(1,4), restored(1,3), nnz(folded))
