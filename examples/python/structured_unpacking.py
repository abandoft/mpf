def payload():
    return ((10, [20, 21, 22, 23]), 42)


(left, [inner_left, *inner_middle, inner_right]), answer = payload()
head, *middle, tail = (1, 2, 3, 4)
start, *empty, end = (20, 22)

print(
    left,
    inner_left,
    sum(inner_middle),
    inner_right,
    answer,
    head,
    sum(middle),
    tail,
    len(empty),
    start + end,
)
