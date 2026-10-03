function output = restricted_input_validator(input)
    arguments
        input (1,1) double {mustBeGreaterThan(nargout)}
    end
    output = input;
end
