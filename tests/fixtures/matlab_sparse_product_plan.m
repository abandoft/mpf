A = sparse([1 0 2; 0 3 0]);
B = sparse([0 4; 5 0; 0 6]);
C = A * B;
disp(nnz(C))
