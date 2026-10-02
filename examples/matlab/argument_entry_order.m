% Each formal is normalized and validated before advancing to the next one.
try
  checked(-1, [1, 2]);
catch exception
  disp(exception.message);
end

try
  checked_optional(-1);
catch exception
  disp(exception.message);
end

% All supplied actual expressions run left to right before entry validation.
try
  checked(marked(-1, 21), marked(2, 22));
catch exception
  disp(exception.message);
end

try
  checked(1, [1, 2]);
catch
  disp(31);
end

disp(checked_optional(3, 4));
disp(checked_optional(3));
disp(derived(5));
disp(size_only([1; 2]));
disp(char_only('abc'));
disp(unconstrained(7));

function output = checked(first, second)
  arguments
    first (1,1) double {mustBePositive}
    second (1,1) double
  end
  output = first + second;
end

function output = checked_optional(first, second)
  arguments
    first (1,1) double {mustBePositive}
    second (1,1) double = fallback()
  end
  output = first + second;
end

function output = marked(value, marker)
  arguments
    value (1,1) double
    marker (1,1) double
  end
  disp(marker);
  output = value;
end

function output = fallback()
  disp(44);
  output = 2;
end

function output = derived(first, second)
  arguments
    first (1,1) logical
    second (1,1) double = first + 2
  end
  output = second;
end

function output = size_only(values)
  arguments
    values (1,:)
  end
  output = length(values);
end

function output = char_only(value)
  arguments
    value (1,:) char
  end
  output = length(value);
end

function output = unconstrained(value)
  arguments
    value
  end
  output = value;
end
