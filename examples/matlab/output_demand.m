% Discarding an output must preserve calls, defaults, validation and failures.
checked();
checked;
disp(ans);
value = checked();
disp(value);
[first, second] = checked();
disp(first);
disp(second);
disp(checked());
disp(with_default());
disp(with_default(9));
try
    body_failure();
catch exception
    disp(exception.identifier);
end
try
    invalid_second();
catch exception
    disp(exception.message);
end

function [first, second] = checked()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) logical {mustBeNonzero}
    end
    disp(111);
    first = 4;
    second = 2;
end

function output = with_default(input)
    arguments
        input (1,1) double = checked()
    end
    disp(222);
    output = input;
end

function output = body_failure()
    arguments (Output)
        output (1,1) double {mustBePositive}
    end
    disp(333);
    output = 0;
    error('MPF:Demand', 'body failure');
end

function [first, second] = invalid_second()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) double {mustBePositive}
    end
    disp(444);
    first = 4;
    second = -1;
end
