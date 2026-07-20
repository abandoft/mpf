A = sparse([1 1; 0 1]);
exponent = -1;
B = A ^ exponent;
disp(nnz(B))
