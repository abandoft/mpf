disp(row_total());
disp(row_total(2));
disp(row_total([1 2]));
disp(scalar_total([3]));
disp(length(2),numel(2),length(complex(2,3)),numel(complex(2,3)));
disp(empty_length([]));
disp(empty_length(reshape([],1,0)));
disp(empty_length(reshape([],0,5)));

function output = row_total(value)
arguments
value (1,:) double = 1
end
output = sum(value);
end

function output = scalar_total(value)
arguments
value (1,1) double
end
output = value + 1;
end

function output = empty_length(value)
arguments
value (:,:) double
end
output = length(value);
end
