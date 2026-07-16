function result = classify(value, offset)
  result = value + offset;
  if result == 42
    result = 42;
  elseif result < 0
    result = 0;
  else
    result = 1;
  end
end

text = 'a=b:c';
answer = classify(40, 2);
for index = 1:2
  if index == 2
    disp(answer)
  end
end
