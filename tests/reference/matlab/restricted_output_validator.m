function output = restricted_output_validator()
    arguments (Output)
        output (1,1) double {mustBeGreaterThan(nargout())}
    end
    output = 7;
end
