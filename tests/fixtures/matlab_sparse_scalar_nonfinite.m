A = sparse([1 0; 0 2]);
factor = 1 / 0;
B = A * factor;
disp(nnz(B))
