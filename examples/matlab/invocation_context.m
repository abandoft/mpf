% Counts are local to each invocation, independent of selected values and source inputs.
counted();
counted;
disp(ans);
one = counted();
disp(one);
[first, second] = counted();
disp(first);
disp(second);
[first, second, third] = counted();
disp(first);
disp(second);
disp(third);
disp(100 + counted());
with_default();
disp(with_default());
[first, second] = with_default();
disp(first);
disp(second);
disp(with_default(9));
disp(recursive(3));
disp(shadowed(42));

% Bracket receivers keep their requested positions, including ignored values.
[one] = counted();
disp(one);
[~, second] = counted();
disp(second);
[first, ~, third] = counted();
disp(first);
disp(third);
[~, ~] = counted();
[~, ~, ~] = counted();
[~] = counted();
[first, ~] = with_default();
disp(first);
[first, ~] = counted();
disp(first);
[~] = with_default(counted());
[scalar] = shadowed(17);
disp(scalar);
[generic] = passthrough(19);
disp(generic);
[builtin] = sqrt(81);
disp(builtin);
ans = 901;
[~] = counted();
try
    [~, ~] = invalid_discard();
catch
    disp(404);
end
try
    [first, ~] = invalid_discard();
catch
    disp(405);
end
try
    [~] = invalid_discard();
catch
    disp(406);
end
disp(ans);

function [first, second, third] = counted()
    disp(nargout);
    first = nargout;
    second = nargout() + 10;
    third = nargout + 20;
end

function [first, second] = with_default(input)
    arguments
        input (1,1) double = default_input()
    end
    arguments (Output)
        first (1,1) double {mustBeNonnegative}
        second (1,1) double {mustBePositive}
    end
    disp(nargout);
    first = input;
    second = nargout + 100;
end

function output = recursive(depth)
    arguments
        depth (1,1) double
    end
    arguments (Output)
        output (1,1) double
    end
    disp(nargout);
    output = nargout;
    if depth > 0
        recursive(depth - 1);
        output = output + nargout;
    end
end

function output = shadowed(nargout)
    output = nargout;
end

function output = default_input()
    % This separate workspace receives one requested output from the default expression.
    output = nargout + 6;
end

function [first, second] = invalid_discard()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) double {mustBePositive}
    end
    disp(nargout);
    first = 4;
    second = -1;
end

function [first, second] = passthrough(input)
    first = input;
    second = input + 1;
end
