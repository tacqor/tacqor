// Hand-tuned C++ inner product, same shape as Tacqor's dot() builtin (blocked accumulation),
// as the ceiling for benchmarks/vdot.tq. Build with -O3; add -march=native for AVX.
#include <cstdio>
#include <vector>
#include <chrono>
int main() {
    const long n = 16384;
    std::vector<double> xs(n, 1.5), ys(n, 2.0);
    const int reps = 20000;
    double acc = 0.0;
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) {
        const double* x = xs.data();
        const double* y = ys.data();
        double a[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        long i = 0;
        for (; i + 8 <= n; i += 8)
            for (int k = 0; k < 8; ++k) a[k] += x[i + k] * y[i + k];
        double s = ((a[0] + a[1]) + (a[2] + a[3])) + ((a[4] + a[5]) + (a[6] + a[7]));
        for (; i < n; ++i) s += x[i] * y[i];
        acc += s;
    }
    auto t1 = std::chrono::steady_clock::now();
    // Print the sum so the compiler cannot discard the loop; timing is done by the runner.
    (void) t1; (void) t0;
    printf("%g\n", acc);
    return 0;
}
