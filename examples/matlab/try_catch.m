value = 0;
try
  error('MPF:Expected', 'boom')
catch ME
  disp(ME.identifier)
  value = 40;
end

try
  value = value + 2;
catch
  value = 0;
end

try
  try
    error('MPF:Nested', 'nested')
  catch inner
    rethrow(inner)
  end
catch outer
  disp(outer.message)
end

disp(value)
