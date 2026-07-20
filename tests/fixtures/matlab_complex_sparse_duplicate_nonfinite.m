x = complex(1e308, 1e308);
A = sparse([1 1], [1 1], [x x], 1, 1);
disp(nnz(A))
