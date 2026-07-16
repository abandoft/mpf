values = [0, 1, 2, 3, 4, 5]
forward = values[1:100:2]
reverse = values[::-2]
print(len(forward), sum(forward), len(reverse), sum(reverse))
