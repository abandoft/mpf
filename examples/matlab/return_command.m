disp start
display 'hello world'
disp(classify(2))
disp(classify(-1))
[first, second] = pair(1);
disp(first)
disp(second)
disp(7)
return
disp(999)

function y = classify(x)
  y = -1;
  if x > 0
    y = 42;
    return
  end
  y = 0;
end

function [first, second] = pair(x)
  first = x;
  second = x + 1;
  if x > 0
    return
  end
  first = 0;
  second = 0;
end
