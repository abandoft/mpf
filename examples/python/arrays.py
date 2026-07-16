def first(items):
    return items[0]

values = [1, 2, 3]
values[1] = values[0] + 4
values[-1] = values[-1] + 7
print(len(values), sum(values), values[1], values[-1], first(values))
