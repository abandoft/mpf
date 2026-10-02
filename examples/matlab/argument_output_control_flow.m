% Output validation belongs to the return boundary, outside the function body.
try
    disp(invalid_early());
catch
    disp(999);
end

[first, second] = pair(false);
disp(first);
disp(second);
[first, second] = pair(true);
disp(first);
disp(second);
disp(loop_return());
disp(handler_return());
try
    disp(nested_return());
catch
    disp(998);
end
try
    disp(body_failure());
catch exception
    disp(exception.identifier);
end
disp(updated_threshold(1));
disp(shared_name(2));
first = pair(false);
disp(first);
try
    [failed_first, failed_second] = first_failure();
    disp(failed_first);
    disp(failed_second(1,1));
catch exception
    disp(exception.message);
end

[partial_first, partial_second] = triple();
disp(partial_first);
disp(partial_second);
try
    selected = unrequested_failure();
    disp(selected);
catch exception
    disp(exception.message);
end
try
    unrequested_failure();
catch exception
    disp(exception.message);
end

function output = invalid_early()
    arguments (Output)
        output (1,1) logical {mustBeNonzero}
    end
    output = 0;
    try
        return;
    catch
        disp(111);
    end
    output = 1;
end

function [first, second, third] = triple()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) logical {mustBeNonzero}
        third (1,2) double {mustBePositive}
    end
    first = 4;
    second = 2;
    third = [8,9];
end

function [first, second] = unrequested_failure()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) double {mustBePositive}
    end
    first = 4;
    second = -1;
end

function [first, second] = pair(early)
    arguments
        early (1,1) logical
    end
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) logical {mustBeNonzero}
    end
    first = 2;
    second = 3;
    if early
        return;
    end
    first = 6;
    second = 7;
end

function output = loop_return()
    arguments (Output)
        output (1,1) logical {mustBeNonzero}
    end
    output = 0;
    for index = 1:3
        if index == 2
            output = 4;
            return;
        end
    end
end

function output = handler_return()
    arguments (Output)
        output (1,1) double {mustBePositive}
    end
    output = 0;
    try
        error('MPF:Handled', 'handled');
    catch
        output = 12;
        return;
    end
end

function output = nested_return()
    arguments (Output)
        output (1,1) logical {mustBeNonzero}
    end
    output = 0;
    try
        try
            return;
        catch
            disp(112);
        end
    catch
        disp(113);
    end
    output = 1;
end

function output = body_failure()
    arguments (Output)
        output (1,1) double {mustBePositive}
    end
    output = 0;
    error('MPF:Body', 'body failure');
end

function output = updated_threshold(limit)
    arguments
        limit (1,1) double
    end
    arguments (Output)
        output (1,1) double {mustBeLessThan(output,limit)}
    end
    limit = 9.0;
    output = 7;
    return;
end

function value = shared_name(value)
    arguments
        value (1,1) double
    end
    arguments (Output)
        value (1,1) logical
    end
    return;
end

function [first, second] = first_failure()
    arguments (Output)
        first (1,1) double {mustBePositive,mustBeNonzero}
        second (1,2) double
    end
    first = -1;
    second = [1 2 3];
    return;
end
