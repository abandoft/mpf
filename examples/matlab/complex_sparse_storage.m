dense = [1+2i 0; 0 3-4i];
converted = sparse(dense);
triplets = sparse([1 1 2], [1 1 2], [1+2i 3-2i -4i], 2, 2, 5);
copied = sparse(triplets);

plain = triplets.';
hermitian = triplets';
selected = triplets([2 1], [2 1]);
reshaped = reshape(selected, [1 4]);

triplets(1, 2) = 5-6i;
triplets(2, 2) = 0;
grown = triplets;
grown(3, 3) = 7+8i;

dense_converted = full(converted);
dense_plain = full(plain);
dense_hermitian = full(hermitian);
dense_selected = full(selected);
dense_reshaped = full(reshaped);
dense_mutated = full(triplets);
dense_grown = full(grown);

disp(nnz(converted), nnz(copied), nnz(triplets), 0 + issparse(selected), ...
     0 + issparse(reshaped), real(dense_converted(1, 1)), ...
     imag(dense_converted(1, 1)), real(dense_plain(1, 1)), ...
     imag(dense_plain(2, 2)), real(dense_hermitian(1, 1)), ...
     imag(dense_hermitian(2, 2)), imag(dense_selected(1, 1)), ...
     imag(dense_reshaped(1, 1)), real(dense_reshaped(1, 4)), ...
     real(dense_mutated(1, 2)), imag(dense_mutated(1, 2)), ...
     nnz(grown), real(dense_grown(3, 3)), imag(dense_grown(3, 3)))
