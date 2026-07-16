def probe(value):
    print(value)
    return value


first = 1 < probe(2) < 3
second = 3 < probe(2) < probe(99)
choice = probe(40) + 2 if first and not second else probe(0)
nested = 1 if False else 2 if True else probe(98)
values = [1, 2] if first else [9]
numeric_equality = 7 if True == 1 == 1.0 else 0
sequence_equality = 8 if [1, 2] == [1, 2] != [2, 1] else 0
print(choice, nested, sum(values), numeric_equality, sequence_equality)
