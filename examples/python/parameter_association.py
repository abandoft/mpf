def combine(left, /, right=2, *, scale=1):
    return (left + right) * scale


def forward(value, /, *, increment=1):
    return combine(value, right=increment, scale=1)


def required(value, /, *, offset):
    return value + offset


def format_label(prefix="answer", *, suffix="!"):
    return prefix + suffix


print(
    combine(40),
    combine(40, scale=2),
    combine(40, right=1, scale=2),
    combine(40, scale=2, right=1),
    forward(40),
    forward(40, increment=2),
    required(40, offset=2),
    format_label(suffix="?"),
)
