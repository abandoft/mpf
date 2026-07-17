values = [10 20 30 40];
matrix = [1 2 3; 4 5 6];
tail = values(2:end);
disp(values(end) + values(end - 1) + tail(end) + matrix(end, end) + matrix(end))
