total = 0
if []:
    total = 999
if [0]:
    total = total + 2
if float("nan"):
    total = total + 4
if "":
    total = total + 8
if "value":
    total = total + 16
if not []:
    total = total + 32
if not [0]:
    total = total + 64
if not float("nan"):
    total = total + 128
if not 0:
    total = total + 256
if not "":
    total = total + 512
chosen = [1] and [2]
total = total + sum(chosen)
chosen_text = "" or "value"
if chosen_text:
    total = total + 1024
while []:
    total = 999

def zero():
    print(1)
    return 0

def two():
    print(2)
    return 2

print(total)
print(zero() and two())
print(two() or zero())
