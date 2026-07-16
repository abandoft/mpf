cube = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]
plane = cube[1]
row = cube[0][1]
cube[1][0][1] = 9
print(len(cube), len(plane), sum(row), cube[1][0][1])
