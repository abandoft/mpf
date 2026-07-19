A = sparse([1 0 -2; 0 3 0]);
right_scaled = A * 4;
left_scaled = -0.5 * A;
zero_scaled = A * 0;
right_full = full(right_scaled);
left_full = full(left_scaled);
disp(right_full(1, 1), right_full(1, 3), right_full(2, 2), ...
     left_full(1, 1), left_full(1, 3), left_full(2, 2), ...
     0 + issparse(right_scaled), 0 + issparse(left_scaled), ...
     0 + issparse(zero_scaled), nnz(right_scaled), nnz(zero_scaled))
