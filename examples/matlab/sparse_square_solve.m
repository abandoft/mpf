tridiagonal = sparse([2 1 0; 0 3 1; 0 0 4]);
dense_rhs = [4; 9; 12];
dense_solution = tridiagonal \ dense_rhs;

pivoted = sparse([0 2 0; 1 3 1; 0 1 4]);
sparse_rhs = sparse([4; 10; 14]);
sparse_solution = pivoted \ sparse_rhs;

right_coefficient = sparse([1 2 0; 0 3 1; 2 0 4]);
sparse_left = sparse([7 8 14; 16 23 29]);
sparse_quotient = sparse_left / right_coefficient;

dense_sparse_solution = full(sparse_solution);
dense_sparse_quotient = full(sparse_quotient);
storage_flags = 0 + issparse(tridiagonal) + issparse(dense_solution) + ...
                issparse(sparse_solution) + issparse(sparse_quotient);

disp(storage_flags, nnz(tridiagonal), nnz(pivoted), nnz(full(tridiagonal)), ...
     dense_solution(1), dense_solution(2), dense_solution(3), ...
     dense_sparse_solution(1), dense_sparse_solution(2), dense_sparse_solution(3), ...
     dense_sparse_quotient(1, 1), dense_sparse_quotient(1, 2), ...
     dense_sparse_quotient(1, 3), dense_sparse_quotient(2, 1), ...
     dense_sparse_quotient(2, 2), dense_sparse_quotient(2, 3))
