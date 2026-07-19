coefficient = sparse([1 2; 2 4]);
solution = coefficient \ [3; 6];

disp(0 + issparse(solution), nnz(coefficient))
