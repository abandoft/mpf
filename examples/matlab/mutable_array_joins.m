% All values retain the same numeric array type, but their extents may change.
for mode = 0:1
    values = [1, 2];
    try
        values = [3, 4, 5, 6];
        if mode == 1
            error('MPF:Join', 'body');
        end
    catch exception
        values = [7, 8, 9];
    end
    disp(numel(values));
end

condition = 1;
values = [1, 2];
if condition == 1
    values = [1, 2, 3, 4, 5];
else
    values = [1, 2, 3];
end
disp(numel(values));

values = [1, 2];
for step = 1:3
    values = [step, step + 1, step + 2];
end
disp(numel(values));
