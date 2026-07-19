A = sparse([1 0; 0 2]);
A(1) = 1 / 0;
disp(nnz(A))
