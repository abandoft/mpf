items = [1, 2]
pair = (1, "x")

membership = 10 if 1 in items and 3 not in pair else 0
substring = 20 if "bc" in "abcd" and "z" not in "abcd" else 0
singletons = 3 if None is None and True is not False else 0
sequence_kinds = 4 if [1, 2] != (1, 2) else 0
mixed_equality = 5 if 1 != "1" else 0
chain = 1 in items is not None
recursive_list_equality = 7 if [True] == [1] else 0
recursive_tuple_equality = 9 if (True, (2,)) == (1, (2.0,)) else 0
numeric_membership = 8 if True in [1] and 2.0 in (2,) else 0

print(
    membership,
    substring,
    singletons,
    sequence_kinds,
    mixed_equality,
    6 if chain else 0,
    recursive_list_equality,
    recursive_tuple_equality,
    numeric_membership,
)
