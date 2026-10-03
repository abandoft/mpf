function evidence = mpf_validator_semantics()
% Record native identities; do not derive them from validator spelling.
    operations = {
        'numeric', 'mustBeNumeric', @() mustBeNumeric('text'), ...
            @() input_numeric('text'), @() output_numeric('text');
        'numeric_or_logical', 'mustBeNumericOrLogical', @() mustBeNumericOrLogical('text'), ...
            @() input_numeric_or_logical('text'), @() output_numeric_or_logical('text');
        'floating', 'mustBeFloat', @() mustBeFloat(false), ...
            @() input_floating(false), @() output_floating(false);
        'real', 'mustBeReal', @() mustBeReal(complex(1,1)), ...
            @() input_real(complex(1,1)), @() output_real(complex(1,1));
        'finite', 'mustBeFinite', @() mustBeFinite(Inf), ...
            @() input_finite(Inf), @() output_finite(Inf);
        'non_nan', 'mustBeNonNan', @() mustBeNonNan(NaN), ...
            @() input_non_nan(NaN), @() output_non_nan(NaN);
        'positive', 'mustBePositive', @() mustBePositive(-1), ...
            @() input_positive(-1), @() output_positive(-1);
        'nonpositive', 'mustBeNonpositive', @() mustBeNonpositive(1), ...
            @() input_nonpositive(1), @() output_nonpositive(1);
        'nonnegative', 'mustBeNonnegative', @() mustBeNonnegative(-1), ...
            @() input_nonnegative(-1), @() output_nonnegative(-1);
        'negative', 'mustBeNegative', @() mustBeNegative(1), ...
            @() input_negative(1), @() output_negative(1);
        'nonzero', 'mustBeNonzero', @() mustBeNonzero(0), ...
            @() input_nonzero(0), @() output_nonzero(0);
        'integer', 'mustBeInteger', @() mustBeInteger(0.5), ...
            @() input_integer(0.5), @() output_integer(0.5);
        'nonempty', 'mustBeNonempty', @() mustBeNonempty([]), ...
            @() input_nonempty([]), @() output_nonempty([]);
        'scalar_or_empty', 'mustBeScalarOrEmpty', @() mustBeScalarOrEmpty([1,2]), ...
            @() input_scalar_or_empty([1,2]), @() output_scalar_or_empty([1,2]);
        'vector', 'mustBeVector', @() mustBeVector([1,2;3,4]), ...
            @() input_vector([1,2;3,4]), @() output_vector([1,2;3,4]);
        'row', 'mustBeRow', @() mustBeRow([1;2]), ...
            @() input_row([1;2]), @() output_row([1;2]);
        'column', 'mustBeColumn', @() mustBeColumn([1,2]), ...
            @() input_column([1,2]), @() output_column([1,2]);
        'matrix', 'mustBeMatrix', @() mustBeMatrix(ones(2,2,2)), ...
            @() input_matrix(ones(2,2,2)), @() output_matrix(ones(2,2,2));
        'nonmissing', 'mustBeNonmissing', @() mustBeNonmissing(NaN), ...
            @() input_nonmissing(NaN), @() output_nonmissing(NaN);
        'nonzero_length_text', 'mustBeNonzeroLengthText', @() mustBeNonzeroLengthText(''), ...
            @() input_nonzero_length_text(''), @() output_nonzero_length_text('');
        'text', 'mustBeText', @() mustBeText(1), ...
            @() input_text(1), @() output_text(1);
        'text_scalar', 'mustBeTextScalar', @() mustBeTextScalar(1), ...
            @() input_text_scalar(1), @() output_text_scalar(1);
        'valid_variable_name', 'mustBeValidVariableName', @() mustBeValidVariableName('1bad'), ...
            @() input_valid_variable_name('1bad'), @() output_valid_variable_name('1bad');
        'greater_than', 'mustBeGreaterThan', @() mustBeGreaterThan(0,1), ...
            @() input_greater_than(0), @() output_greater_than(0);
        'greater_than_or_equal', 'mustBeGreaterThanOrEqual', @() mustBeGreaterThanOrEqual(0,1), ...
            @() input_greater_than_or_equal(0), @() output_greater_than_or_equal(0);
        'less_than', 'mustBeLessThan', @() mustBeLessThan(2,1), ...
            @() input_less_than(2), @() output_less_than(2);
        'less_than_or_equal', 'mustBeLessThanOrEqual', @() mustBeLessThanOrEqual(2,1), ...
            @() input_less_than_or_equal(2), @() output_less_than_or_equal(2);
        'in_range', 'mustBeInRange', @() mustBeInRange(3,0,1), ...
            @() input_in_range(3), @() output_in_range(3);
        'positive-complex', 'mustBePositive', @() mustBePositive(complex(1,1)), ...
            @() input_positive(complex(1,1)), @() output_positive(complex(1,1));
        'positive-text', 'mustBePositive', @() mustBePositive('a'), ...
            @() input_positive('a'), @() output_positive('a');
        'integer-complex', 'mustBeInteger', @() mustBeInteger(complex(1,1)), ...
            @() input_integer(complex(1,1)), @() output_integer(complex(1,1));
        'integer-text', 'mustBeInteger', @() mustBeInteger('a'), ...
            @() input_integer('a'), @() output_integer('a');
        'finite-text', 'mustBeFinite', @() mustBeFinite('a'), ...
            @() input_finite('a'), @() output_finite('a');
        'nonmissing-text', 'mustBeNonmissing', @() mustBeNonmissing('a'), ...
            @() input_nonmissing('a'), @() output_nonmissing('a');
        'nonzero-length-text-type', 'mustBeNonzeroLengthText', @() mustBeNonzeroLengthText(1), ...
            @() input_nonzero_length_text(1), @() output_nonzero_length_text(1);
        'valid-variable-name-type', 'mustBeValidVariableName', @() mustBeValidVariableName(1), ...
            @() input_valid_variable_name(1), @() output_valid_variable_name(1);
        'greater-than-complex', 'mustBeGreaterThan', @() mustBeGreaterThan(complex(2,1),1), ...
            @() input_greater_than(complex(2,1)), @() output_greater_than(complex(2,1));
        'greater-than-text', 'mustBeGreaterThan', @() mustBeGreaterThan('a',1), ...
            @() input_greater_than('a'), @() output_greater_than('a');
        'nonzero-length-text-empty-double', 'mustBeNonzeroLengthText', @() mustBeNonzeroLengthText([]), ...
            @() input_nonzero_length_text([]), @() output_nonzero_length_text([]);
        'text-scalar-char-matrix', 'mustBeTextScalar', @() mustBeTextScalar(['ab';'cd']), ...
            @() input_text_scalar(['ab';'cd']), @() output_text_scalar(['ab';'cd']);
        'range-exclusive', 'mustBeInRange', @() mustBeInRange(0,0,1,'exclusive'), ...
            @() input_range_exclusive(0), @() output_range_exclusive(0);
        'range-exclude-lower', 'mustBeInRange', @() mustBeInRange(0,0,1,'exclude-lower'), ...
            @() input_range_exclude_lower(0), @() output_range_exclude_lower(0);
        'range-exclude-upper', 'mustBeInRange', @() mustBeInRange(1,0,1,'exclude-upper'), ...
            @() input_range_exclude_upper(1), @() output_range_exclude_upper(1);
    };
    contexts = {'direct', 'input', 'output'};
    cases = repmat(empty_case(), 0, 1);
    for row = 1:size(operations, 1)
        for context = 1:numel(contexts)
            entry = empty_case();
            entry.name = [operations{row, 1}, '-', contexts{context}];
            entry.validator = operations{row, 2};
            entry.context = contexts{context};
            try
                operation = operations{row, context + 2};
                operation();
                entry.succeeded = true;
            catch exception
                entry.exceptionIdentifier = exception.identifier;
                entry.exceptionMessage = exception.message;
                entry.causeIdentifiers = cellfun(@(cause) cause.identifier, ...
                    exception.cause, 'UniformOutput', false);
            end
            cases(end + 1) = entry;
        end
    end
    evidence = struct('schemaVersion', 1, 'cases', cases);
end

function entry = empty_case()
    entry = struct('name', '', 'validator', '', 'context', '', 'succeeded', false, ...
        'exceptionIdentifier', '', 'exceptionMessage', '', 'causeIdentifiers', {{}});
end

function input_numeric(value)
    arguments
        value {mustBeNumeric}
    end
end

function result = output_numeric(value)
    arguments (Output)
        result {mustBeNumeric}
    end
    result = value;
end

function input_numeric_or_logical(value)
    arguments
        value {mustBeNumericOrLogical}
    end
end

function result = output_numeric_or_logical(value)
    arguments (Output)
        result {mustBeNumericOrLogical}
    end
    result = value;
end

function input_floating(value)
    arguments
        value {mustBeFloat}
    end
end

function result = output_floating(value)
    arguments (Output)
        result {mustBeFloat}
    end
    result = value;
end

function input_real(value)
    arguments
        value {mustBeReal}
    end
end

function result = output_real(value)
    arguments (Output)
        result {mustBeReal}
    end
    result = value;
end

function input_finite(value)
    arguments
        value {mustBeFinite}
    end
end

function result = output_finite(value)
    arguments (Output)
        result {mustBeFinite}
    end
    result = value;
end

function input_non_nan(value)
    arguments
        value {mustBeNonNan}
    end
end

function result = output_non_nan(value)
    arguments (Output)
        result {mustBeNonNan}
    end
    result = value;
end

function input_positive(value)
    arguments
        value {mustBePositive}
    end
end

function result = output_positive(value)
    arguments (Output)
        result {mustBePositive}
    end
    result = value;
end

function input_nonpositive(value)
    arguments
        value {mustBeNonpositive}
    end
end

function result = output_nonpositive(value)
    arguments (Output)
        result {mustBeNonpositive}
    end
    result = value;
end

function input_nonnegative(value)
    arguments
        value {mustBeNonnegative}
    end
end

function result = output_nonnegative(value)
    arguments (Output)
        result {mustBeNonnegative}
    end
    result = value;
end

function input_negative(value)
    arguments
        value {mustBeNegative}
    end
end

function result = output_negative(value)
    arguments (Output)
        result {mustBeNegative}
    end
    result = value;
end

function input_nonzero(value)
    arguments
        value {mustBeNonzero}
    end
end

function result = output_nonzero(value)
    arguments (Output)
        result {mustBeNonzero}
    end
    result = value;
end

function input_integer(value)
    arguments
        value {mustBeInteger}
    end
end

function result = output_integer(value)
    arguments (Output)
        result {mustBeInteger}
    end
    result = value;
end

function input_nonempty(value)
    arguments
        value {mustBeNonempty}
    end
end

function result = output_nonempty(value)
    arguments (Output)
        result {mustBeNonempty}
    end
    result = value;
end

function input_scalar_or_empty(value)
    arguments
        value {mustBeScalarOrEmpty}
    end
end

function result = output_scalar_or_empty(value)
    arguments (Output)
        result {mustBeScalarOrEmpty}
    end
    result = value;
end

function input_vector(value)
    arguments
        value {mustBeVector}
    end
end

function result = output_vector(value)
    arguments (Output)
        result {mustBeVector}
    end
    result = value;
end

function input_row(value)
    arguments
        value {mustBeRow}
    end
end

function result = output_row(value)
    arguments (Output)
        result {mustBeRow}
    end
    result = value;
end

function input_column(value)
    arguments
        value {mustBeColumn}
    end
end

function result = output_column(value)
    arguments (Output)
        result {mustBeColumn}
    end
    result = value;
end

function input_matrix(value)
    arguments
        value {mustBeMatrix}
    end
end

function result = output_matrix(value)
    arguments (Output)
        result {mustBeMatrix}
    end
    result = value;
end

function input_nonmissing(value)
    arguments
        value {mustBeNonmissing}
    end
end

function result = output_nonmissing(value)
    arguments (Output)
        result {mustBeNonmissing}
    end
    result = value;
end

function input_nonzero_length_text(value)
    arguments
        value {mustBeNonzeroLengthText}
    end
end

function result = output_nonzero_length_text(value)
    arguments (Output)
        result {mustBeNonzeroLengthText}
    end
    result = value;
end

function input_text(value)
    arguments
        value {mustBeText}
    end
end

function result = output_text(value)
    arguments (Output)
        result {mustBeText}
    end
    result = value;
end

function input_text_scalar(value)
    arguments
        value {mustBeTextScalar}
    end
end

function result = output_text_scalar(value)
    arguments (Output)
        result {mustBeTextScalar}
    end
    result = value;
end

function input_valid_variable_name(value)
    arguments
        value {mustBeValidVariableName}
    end
end

function result = output_valid_variable_name(value)
    arguments (Output)
        result {mustBeValidVariableName}
    end
    result = value;
end

function input_greater_than(value)
    arguments
        value {mustBeGreaterThan(value,1)}
    end
end

function result = output_greater_than(value)
    arguments (Output)
        result {mustBeGreaterThan(result,1)}
    end
    result = value;
end

function input_greater_than_or_equal(value)
    arguments
        value {mustBeGreaterThanOrEqual(value,1)}
    end
end

function result = output_greater_than_or_equal(value)
    arguments (Output)
        result {mustBeGreaterThanOrEqual(result,1)}
    end
    result = value;
end

function input_less_than(value)
    arguments
        value {mustBeLessThan(value,1)}
    end
end

function result = output_less_than(value)
    arguments (Output)
        result {mustBeLessThan(result,1)}
    end
    result = value;
end

function input_less_than_or_equal(value)
    arguments
        value {mustBeLessThanOrEqual(value,1)}
    end
end

function result = output_less_than_or_equal(value)
    arguments (Output)
        result {mustBeLessThanOrEqual(result,1)}
    end
    result = value;
end

function input_in_range(value)
    arguments
        value {mustBeInRange(value,0,1)}
    end
end

function result = output_in_range(value)
    arguments (Output)
        result {mustBeInRange(result,0,1)}
    end
    result = value;
end

function input_range_exclusive(value)
    arguments
        value {mustBeInRange(value,0,1,'exclusive')}
    end
end

function result = output_range_exclusive(value)
    arguments (Output)
        result {mustBeInRange(result,0,1,'exclusive')}
    end
    result = value;
end

function input_range_exclude_lower(value)
    arguments
        value {mustBeInRange(value,0,1,'exclude-lower')}
    end
end

function result = output_range_exclude_lower(value)
    arguments (Output)
        result {mustBeInRange(result,0,1,'exclude-lower')}
    end
    result = value;
end

function input_range_exclude_upper(value)
    arguments
        value {mustBeInRange(value,0,1,'exclude-upper')}
    end
end

function result = output_range_exclude_upper(value)
    arguments (Output)
        result {mustBeInRange(result,0,1,'exclude-upper')}
    end
    result = value;
end
