# The same inner product in pure CPython (no numpy), for the language-level comparison.
xs = [1.5] * 16384
ys = [2.0] * 16384
acc = 0.0
for _ in range(20000):
    s = 0.0
    for i in range(16384):
        s += xs[i] * ys[i]
    acc += s
print(acc)
