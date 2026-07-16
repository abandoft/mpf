[value, doubled] = outer(20);
disp(value + doubled)

function [value, doubled] = outer(input)
  [value, doubled] = inner(input);
end

function [value, doubled] = inner(input)
  value = input + 1;
  doubled = input * 2;
end
