function output = restricted_default_bare(input)
% Deliberately forbidden argument-block query; load through a caught observation.
    arguments
        input (1,1) double = nargout
    end
    output = input;
end
