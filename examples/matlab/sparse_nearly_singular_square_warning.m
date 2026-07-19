coefficient = sparse([1 0; 0 1e-18]);
solution = coefficient \ [1; 1e-18];

disp(solution(1), 0 + issparse(solution), nnz(coefficient))
