def score(n):
    if n >= 0 and n < 100000000:
        return (n * 48271 + 17) % 2147483647
    return 0
def main():
    total = 0
    for i in range(12000000):
        total = total + score(i)
    print(total)
main()
