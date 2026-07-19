A = sparse([1 0 2; 0 3 0; 4 0 5]);
A(2, 1) = 7;
A(1, 3) = 0;
A([1 3 1]) = [8 9 10];
A(2:3, [2 3]) = [0 11; 12 0];
A(4, 4) = 13;
A(:, 2) = [];

B = sparse([1 2; 0 3]);
B([1 4]) = B([2 3]);
B([1 1 2]) = [4 5 0];

C = sparse(reshape([1 2 0], 1, 3));
C([2 4]) = [0 6];
C(1) = [];

D = sparse([1; 0; 2]);
D([2 4]) = 7;
D([1 3]) = [];

dense_a = full(A);
dense_b = full(B);
dense_c = full(C);
dense_d = full(D);
disp(dense_a(1, 1), dense_a(2, 1), dense_a(3, 1), dense_a(2, 2), ...
     dense_a(4, 3), dense_a(1, 2), nnz(A), 0 + issparse(A), ...
     dense_b(1, 1), dense_b(1, 2), dense_b(2, 2), nnz(B), ...
     dense_c(1, 3), nnz(C), length(dense_c), ...
     dense_d(1, 1), dense_d(2, 1), nnz(D), length(dense_d))
