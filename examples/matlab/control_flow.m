total = 0;
for index = 1:10
  if index == 2
    continue
  elseif index == 5
    break
  else
    total = total + index;
  end
end
disp(total + index)
