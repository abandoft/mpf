A = sparse([1+2i 0; 0 3-4i]);
B = A + A;
disp(nnz(B))
