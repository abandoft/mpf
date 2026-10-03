% JavaScript permits scalar/array rebinding; C++ currently rejects its ABI.
for mode = 0:1
    value = 7;
    try
        value = [11, 13];
        if mode == 1
            error('MPF:Join', 'body');
        end
    catch exception
        value = 19;
    end
    disp(numel(value));
end

for mode = 0:1
    value = [1, 2];
    if mode == 0
        value = 4;
    end
    disp(numel(value));
end

value = 0;
for step = 1:3
    value = [step, step + 1, step + 2];
end
disp(numel(value));
