completed = 0
for outer in range(2):
    for inner in range(3):
        break
    else:
        completed = 100
else:
    completed = completed + 1

print(completed, outer, inner)
