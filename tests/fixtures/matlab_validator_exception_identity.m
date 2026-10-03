% Native R2024b and independent target exception identity parity.
try
    input_numeric('text');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_numeric('text');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_numeric_or_logical('text');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_numeric_or_logical('text');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_floating(false);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_floating(false);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_real(complex(1,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_real(complex(1,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_finite(1 / 0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_finite(1 / 0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_non_nan(0 / 0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_non_nan(0 / 0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_positive(-1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_positive(-1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_nonpositive(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_nonpositive(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_nonnegative(-1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_nonnegative(-1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_negative(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_negative(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_nonzero(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_nonzero(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_integer(0.5);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_integer(0.5);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_nonempty([]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_nonempty([]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_scalar_or_empty([1,2]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_scalar_or_empty([1,2]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_vector([1,2;3,4]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_vector([1,2;3,4]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_row([1;2]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_row([1;2]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_column([1,2]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_column([1,2]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_matrix(reshape([1,1,1,1,1,1,1,1],2,2,2));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_matrix(reshape([1,1,1,1,1,1,1,1],2,2,2));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_nonmissing(0 / 0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_nonmissing(0 / 0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_nonzero_length_text('');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_nonzero_length_text('');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_text(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_text(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_text_scalar(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_text_scalar(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_valid_variable_name('1bad');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_valid_variable_name('1bad');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_greater_than(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_greater_than(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_greater_than_or_equal(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_greater_than_or_equal(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_less_than(2);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_less_than(2);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_less_than_or_equal(2);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_less_than_or_equal(2);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_in_range(3);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_in_range(3);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_positive(complex(1,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_positive(complex(1,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_positive('a');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_positive('a');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_integer(complex(1,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_integer(complex(1,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_integer('a');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_integer('a');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_valid_variable_name(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_valid_variable_name(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_greater_than(complex(2,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_greater_than(complex(2,1));
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_greater_than('a');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_greater_than('a');
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_nonzero_length_text([]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_nonzero_length_text([]);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_range_exclusive(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_range_exclusive(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_range_exclude_lower(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_range_exclude_lower(0);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_range_exclude_upper(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    output_range_exclude_upper(1);
    disp('unexpected-success');
catch exception
    disp(exception.identifier);
end
try
    input_finite('a');
    input_nonmissing('a');
    disp('accepted-text');
catch exception
    disp(exception.identifier);
end
try
    output_finite('a');
    output_nonmissing('a');
    disp('accepted-text');
catch exception
    disp(exception.identifier);
end

% Numeric validators retain MATLAB's empty-value exemption, even for empty char.
try
    input_numeric('');
    input_numeric_or_logical('');
    input_floating('');
    input_positive('');
    input_integer('');
    disp('accepted-empty');
catch exception
    disp(exception.identifier);
end
try
    output_numeric('');
    output_numeric_or_logical('');
    output_floating('');
    output_positive('');
    output_integer('');
    disp('accepted-empty');
catch exception
    disp(exception.identifier);
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
