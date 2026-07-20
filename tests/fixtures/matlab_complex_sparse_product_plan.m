A = sparse([1+2i 0; 0 3-1i]);
B = sparse([2 0; 0 4+1i]);
C = A * B;
disp(nnz(C))
