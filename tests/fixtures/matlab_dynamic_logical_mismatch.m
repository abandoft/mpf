values = [1 2 3];
mask = [true false true];
stop = 2;
dynamic_mask = mask(1:stop);
bad = values(dynamic_mask);
disp(bad)
