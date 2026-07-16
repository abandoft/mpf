values = [1 2 3];
values(2) = values(1) + 4;
values(3) = values(3) + 7;
disp(length(values) + sum(values) + values(2))
