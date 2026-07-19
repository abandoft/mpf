A = sparse([true false; false true]);
F = [true false; true true];
B = A | F;
disp(nnz(B))
