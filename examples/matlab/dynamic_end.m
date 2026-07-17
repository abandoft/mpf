values = [10 20 30 40];
matrix = [1 2 3; 4 5 6];
[last, tail_total, corner_total, selected_total] = inspect_dynamic(values, matrix);
write_total = write_dynamic(values, matrix);
disp(last + tail_total + corner_total + selected_total + write_total)

function [last, tail_total, corner_total, selected_total] = inspect_dynamic(values, matrix)
  last = values(end);
  tail_total = sum(values(2:end));
  corner_total = matrix(end, end) + matrix(end);
  selected_total = sum(values([1 end]));
end

function total = write_dynamic(values, matrix)
  values(end) = values(end) + 1;
  values(2:end - 1) = 5;
  matrix(end, end) = matrix(end, end) + 1;
  total = sum(values) + matrix(end, end);
end
