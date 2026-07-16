[first, second] = pair(6, 7);
single = pair(20, 22);
disp(first + second + single)

function [sum_value, product_value] = pair(left, right)
  sum_value = left + right;
  product_value = left * right;
end
