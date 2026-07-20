A = sparse([true false; false true]);
B = all(A);
disp(nnz(B))
