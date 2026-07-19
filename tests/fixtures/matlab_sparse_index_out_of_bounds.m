A = sparse([1 0 2; 0 3 0; 4 0 5]);
rows = [1 4];
selected = A(rows, :);
disp(nnz(selected))
