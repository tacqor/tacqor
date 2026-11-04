def main():
    n = 5000000
    flags = []
    for i in range(n+1): flags.append(True)
    count = 0
    for i in range(2, n+1):
        if flags[i]:
            count += 1
            j = i*i
            while j <= n:
                flags[j] = False
                j += i
    print(count)
main()
