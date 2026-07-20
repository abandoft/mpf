identity hello
disp(ans)
combine 'two words' tail
disp(ans)
identity +token
disp(ans)
identity "quoted"
disp(ans)
identity 'it''s here'
disp(ans)
sink ignored
disp(ans)

function out = identity(value)
  out = value;
end

function out = combine(first, second)
  disp(first)
  out = second;
end

function sink(value)
  disp(value)
end
