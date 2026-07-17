choice = 'beta';
result = 0;

switch choose(choice)
  case 'alpha'
    result = 1;
  case 'beta'
    result = 40;
  otherwise
    result = -1;
end

switch 2
  case 1
    result = 0;
  case 2.0
    result = result + 2;
  otherwise
    result = 0;
end

disp(result)

function output = choose(input)
  output = input;
end
