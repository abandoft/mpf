A = sparse([1 0 2; 0 3 0]);
B = reshape(A, [3 2]);
disp(nnz(B))
