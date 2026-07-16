total = 0
for index in range(1, 10):
    if index == 2:
        continue
    elif index == 5:
        break
    else:
        total = total + index
else:
    total = 999

count = 0
for value in range(3):
    count = count + 1
else:
    count = count + 10

completed = 0
while completed < 3:
    completed = completed + 1
else:
    completed = completed + 10

stopped = 0
while stopped < 3:
    stopped = stopped + 1
    break
else:
    stopped = 100

print(total, index, count, completed, stopped)
