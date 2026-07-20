A = sparse([1e308+1e308i 0; 0 1]);
B = sparse([2 0; 0 1]);
C = A * B;
disp(nnz(C))
