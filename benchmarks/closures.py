xs = list(range(2000000))
ys = list(map(lambda x: x * 3 % 7, xs))
from functools import reduce
print(reduce(lambda a, b: a + b, ys, 0))
