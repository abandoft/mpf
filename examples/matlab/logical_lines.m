% Ellipsis continuation and top-level statement separators.
values = [
          1 2 ...
          3 4]; total = sum(values); extra = 2
continued = 10 + ...
            20
if total == 10, disp(total + continued + extra), end
