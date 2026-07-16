def classify(value, offset,) -> int:
    result = value + offset
    if result == 42:
        return result
    elif result < 0:
        return 0
    else:
        return 1


text = "a=b:c"
answer = classify(40, 2)
if text == "a=b:c":
    print(answer)
