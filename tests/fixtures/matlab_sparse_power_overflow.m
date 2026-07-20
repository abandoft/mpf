A = sparse([1e200 0; 0 1]);
B = A ^ 2;
disp(nnz(B))
