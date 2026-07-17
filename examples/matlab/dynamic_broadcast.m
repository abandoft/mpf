column = [1; 2];
row = [10 20 30];
[expanded, mask, scaled] = expand_dynamic(column, row);
disp(expanded(2, 3) + sum(mask) + scaled(1, 2))
[scalar_sum, scalar_mask, scalar_scaled] = expand_dynamic(20, 22);
disp(scalar_sum + scalar_mask + scalar_scaled)
logical_total = sum([true true false]);
disp(logical_total)

function [expanded, mask, scaled] = expand_dynamic(left, right)
expanded = left + right;
mask = left < right;
scaled = expanded .* 2;
end
