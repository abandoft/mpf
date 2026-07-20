A = sparse([1 1; 0 1]);
D = sparse([2 0; 0 3]);
L = sparse([true false; false true]);
Z = sparse(0, 0);

third = A ^ 3;
fourth = D ^ 4;
identity = A ^ 0;
logical_power = L ^ 2;
empty_identity = Z ^ 0;

third_full = full(third);
fourth_full = full(fourth);
identity_full = full(identity);
logical_full = full(logical_power);

disp(third_full(1, 2), fourth_full(1, 1), fourth_full(2, 2), ...
     identity_full(1, 1), identity_full(2, 2), ...
     logical_full(1, 1), logical_full(2, 2), ...
     0 + issparse(third), 0 + issparse(fourth), ...
     0 + issparse(identity), 0 + issparse(logical_power), ...
     0 + issparse(empty_identity), nnz(third), nnz(identity), ...
     nnz(logical_power), nnz(empty_identity), length(full(empty_identity)))
