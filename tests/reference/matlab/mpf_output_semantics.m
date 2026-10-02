function evidence = mpf_output_semantics()
% Collect native output behavior without substituting Octave or MPF semantics.
    cases = repmat(empty_case(), 0, 1);
    cases(end + 1) = observe('conditional-one', @conditional_count);
    cases(end + 1) = observe('conditional-two', @request_conditional_two);
    cases(end + 1) = observe('validated-conditional-one', @conditional_validated);
    cases(end + 1) = observe('validated-conditional-two', @request_validated_two);
    cases(end + 1) = observe('missing-requested-first', @missing_first);
    cases(end + 1) = observe('missing-requested-second', @request_missing_second);
    cases(end + 1) = observe('ignored-missing-second', @ignore_missing_second);
    cases(end + 1) = observe('ignored-missing-first', @ignore_missing_first);
    cases(end + 1) = observe('ignored-count', @request_ignored_count);
    cases(end + 1) = observe('assigned-unrequested-validation', @invalid_second);
    cases(end + 1) = observe('assigned-discarded-validation', @discard_invalid_second);
    cases(end + 1) = observe('missing-first-versus-second-validator', ...
        @missing_first_invalid_second);
    cases(end + 1) = observe('first-validator-versus-second-missing', ...
        @request_invalid_first_missing_second);
    cases(end + 1) = observe('return-outside-body-handler', @invalid_return);
    cases(end + 1) = observe('shared-input-output', @request_shared_output);
    cases(end + 1) = observe('default-count-one', @default_count);
    cases(end + 1) = observe('default-count-two', @request_default_two);

    ans = 19;
    current_count();
    cases(end + 1) = successful_case('parenthesized-ans', ans);
    ans = 23;
    current_count;
    cases(end + 1) = successful_case('bare-ans', ans);
    ans = 29;
    no_result();
    cases(end + 1) = successful_case('void-ans-retained', ans);
    ans = 31;
    absent_on_zero();
    cases(end + 1) = successful_case('absent-ans-retained', ans);
    ans = 37;
    default_count();
    cases(end + 1) = successful_case('default-count-zero', ans);
    ans = 53;
    try
        invalid_second();
        failure = successful_case('failed-output-ans', ans);
    catch exception
        failure = failed_case('failed-output-ans', exception);
        failure.values = ans;
        failure.valueClass = class(ans);
        failure.valueSize = size(ans);
    end
    cases(end + 1) = failure;
    evidence = struct('schemaVersion', 1, 'cases', cases, ...
        'builtinAvailability', struct('isargout', exist('isargout', 'builtin')));
end

function entry = empty_case()
    entry = struct('name', '', 'succeeded', false, 'values', [], ...
        'valueClass', '', 'valueSize', [], ...
        'exceptionIdentifier', '', 'exceptionMessage', '');
end

function entry = successful_case(name, value)
    entry = empty_case();
    entry.name = name;
    entry.succeeded = true;
    entry.values = value;
    entry.valueClass = class(value);
    entry.valueSize = size(value);
end

function entry = failed_case(name, exception)
    entry = empty_case();
    entry.name = name;
    entry.exceptionIdentifier = exception.identifier;
    entry.exceptionMessage = exception.message;
end

function entry = observe(name, operation)
    try
        value = operation();
        entry = successful_case(name, value);
    catch exception
        entry = failed_case(name, exception);
    end
end

function [first, second] = conditional_count()
    first = nargout;
    if nargout > 1
        second = nargout + 10;
    end
end

function values = request_conditional_two()
    [first, second] = conditional_count();
    values = [first, second];
end

function [first, second] = conditional_validated()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) logical {mustBeNonzero}
    end
    first = 1;
    if nargout > 1
        second = 12;
    end
end

function values = request_validated_two()
    [first, second] = conditional_validated();
    values = [first, second];
end

function output = missing_first()
    return;
end

function [first, second] = missing_second()
    first = nargout;
end

function output = request_missing_second()
    [first, second] = missing_second();
    output = [first, second];
end

function output = ignore_missing_second()
    [output, ~] = missing_second();
end

function [first, second] = missing_first_assigned_second()
    second = 42;
end

function output = ignore_missing_first()
    [~, output] = missing_first_assigned_second();
end

function [first, second, third] = counted_outputs()
    first = nargout;
    second = nargout + 10;
    third = nargout + 20;
end

function values = request_ignored_count()
    [first, ~, third] = counted_outputs();
    values = [first, third];
end

function [first, second] = invalid_second()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) double {mustBePositive}
    end
    first = 1;
    second = -1;
end

function output = discard_invalid_second()
    invalid_second();
    output = 0;
end

function [first, second] = missing_first_invalid_second()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) double {mustBePositive}
    end
    second = -1;
end

function [first, second] = invalid_first_missing_second()
    arguments (Output)
        first (1,1) double {mustBePositive}
        second (1,1) double {mustBePositive}
    end
    first = -1;
end

function output = request_invalid_first_missing_second()
    [first, second] = invalid_first_missing_second();
    output = [first, second];
end

function output = invalid_return()
    arguments (Output)
        output (1,1) double {mustBePositive}
    end
    output = -1;
    try
        return;
    catch
        output = 2;
    end
end

function value = shared_output(value)
    arguments
        value (1,1) double
    end
    arguments (Output)
        value (1,1) logical
    end
    return;
end

function output = request_shared_output()
    output = shared_output(7);
end

function [first, second] = default_count(input)
    arguments
        input (1,1) double = nargout
    end
    first = input;
    second = nargout;
end

function values = request_default_two()
    [first, second] = default_count();
    values = [first, second];
end

function output = current_count()
    output = nargout;
end

function no_result()
    local = 1;
end

function output = absent_on_zero()
    if nargout > 0
        output = 42;
    end
end
