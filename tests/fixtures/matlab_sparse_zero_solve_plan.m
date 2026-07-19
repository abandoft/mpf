coefficient = sparse([], [], []);
right = sparse(0, 3);
left = sparse(2, 0);
solution = coefficient \ right;
quotient = left / coefficient;
disp(nnz(solution), nnz(quotient))
