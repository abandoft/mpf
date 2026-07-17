left = [1 2];
right = [1 2 3];
disp(expand_dynamic(left, right))

function result = expand_dynamic(left, right)
result = left + right;
end
