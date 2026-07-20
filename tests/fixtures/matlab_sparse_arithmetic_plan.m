A = sparse([1 0; 0 2]);
B = sparse([0 3; 4 -2]);
C = A + B;
disp(nnz(C))
