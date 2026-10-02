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

function [first, second, third] = counted()
    disp(nargout);
    first = nargout;
    second = nargout() + 10;
    third = nargout + 20;
end

function [first, second] = with_default(input)
    arguments
        input (1,1) double = nargout
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
