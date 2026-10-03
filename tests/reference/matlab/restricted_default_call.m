function output = restricted_default_call(input)
% A supplied actual must not make this forbidden definition valid.
    arguments
        input (1,1) double = nargout()
    end
    output = input;
end
