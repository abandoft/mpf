def pair(value):
    return value + 1, value + 2


def forward(value):
    return pair(value)


first, second = forward(39)
(first, second) = (second, first)
[left, right] = [20, 22]
single, = (42,)
label, answer = ("answer", 42)
stored = (20, 22)
stored_left, stored_right = stored
stored_list = [20, 22]
list_left, list_right = stored_list
same, same = (1, 42)

print(
    first,
    second,
    left + right,
    single,
    label,
    answer,
    stored_left + stored_right,
    list_left + list_right,
    same,
)
