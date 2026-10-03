values = [1 + 2, 3 - 1];
disp(numel(values));
disp(values(1));
disp(values(2));
values = [1 +2 -3];
disp(numel(values));
disp(values(2));
disp(values(3));
values = [(1 + 2), (5 - 1)];
disp(numel(values));
disp(values(2));
column_values = [1 + 2; 5 - 1];
disp(numel(column_values));
disp(column_values(2));
values = [increment(1 + 2), increment(5 - 1)];
disp(values(1));
disp(values(2));
values = [1 .* 2, 8 ./ 2, 2 .^ 3];
disp(numel(values));
disp(values(1));
disp(values(2));
disp(values(3));

function output = increment(input)
    output = input + 1;
end
