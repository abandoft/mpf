A = sparse([1e308+1e308i 0; 0 1]);
B = A + A;
disp(nnz(B))
