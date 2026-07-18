z = 2i;
w = (1 + z) * (3 - 4j);
negated = -w;
positive = +w;
identity = w ^ 0;
stable_quotient = (1e308 + 1e308i) / (1e308 - 1e308i);
units = i + j;
constructed = complex(3, 4);
conjugated = conj(constructed);
real_conjugated = conj(5);

values = [1+2i 3-4i; 5i 6];
plain = values.';
hermitian = values';
mixed = values + [1 2; 3 4];
mutated = [1 2];
mutated(2) = 3i;
shaped = reshape(mutated, 2, 1);
scaled = scale_complex(1+2i);
scaled_values = scale_complex_elements([1+2i 3]);

disp(real(w), imag(w), real(plain(2,1)), imag(plain(2,1)), ...
     real(hermitian(2,1)), imag(hermitian(2,1)), ...
     real(mixed(1,2)), imag(mixed(1,2)), ...
     imag(negated), real(positive), real(identity), imag(identity), ...
     real(stable_quotient), imag(stable_quotient), imag(units), ...
     real(conjugated), imag(conjugated), real_conjugated, abs(constructed), ...
     real(shaped(1,1)), imag(shaped(2,1)), ...
     real(scaled), imag(scaled), imag(scaled_values(1)), real(scaled_values(2)))

function result = scale_complex(input)
result = input * 2;
end

function result = scale_complex_elements(input)
result = input .* 2;
end
