A = sparse([1 0; 0 2]);
D = [1 / 0 3; 4 5];
B = A .* D;
disp(nnz(B))
