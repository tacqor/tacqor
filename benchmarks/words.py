def main():
    counts = {}
    for i in range(2000000):
        key = "k" + str(i % 1000)
        if key in counts: counts[key] += 1
        else: counts[key] = 1
    print(len(counts)); print(counts["k7"])
main()
