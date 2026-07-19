A = sparse([true false; false true]);
B = sparse([false true; true false]);
F = [true false; true true];
negated = ~A;
sparse_and = A & B;
sparse_full_and = A & F;
full_sparse_and = F & A;
sparse_or = A | B;
sparse_full_or = A | F;

row = sparse([1 1], [1 3], [true true], 1, 3);
column = sparse([1 2], [1 1], [false true], 2, 1);
broadcast_and = row & column;
broadcast_or = row | column;
scalar_and = row & true;
scalar_or = row | false;
empty_not = ~sparse(0, 3);
full_sparse_not = ~sparse(2, 2);

disp(nnz(negated), nnz(sparse_and), nnz(sparse_full_and), nnz(full_sparse_and), ...
     nnz(sparse_or), nnz(sparse_full_or), nnz(broadcast_and), nnz(broadcast_or), ...
     nnz(scalar_and), nnz(scalar_or), nnz(empty_not), nnz(full_sparse_not), ...
     0 + issparse(negated), 0 + issparse(sparse_full_and), ...
     0 + issparse(sparse_or), 0 + issparse(sparse_full_or), ...
     0 + issparse(scalar_and), 0 + issparse(scalar_or))
