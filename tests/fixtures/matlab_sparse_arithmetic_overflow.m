A = sparse([1e308 0; 0 1]);
B = A + A;
disp(nnz(B))
