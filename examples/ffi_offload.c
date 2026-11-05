/* A tiny "accelerator" addressed by integer handles — the exact shape a BLAS, a SIMD
 * kernel, or a CUDA / OpenCL binding takes when it is reached through Tacqor's scalar FFI.
 *
 * Tacqor's FFI marshals scalars (i64, f64, bool) and strings, not arrays, so bulk data
 * lives on the C side and Tacqor refers to it by an i64 handle. A GPU binding looks
 * identical from Tacqor: accel_alloc would cudaMalloc, accel_set would cudaMemcpy, and
 * accel_dot would launch a kernel and copy one scalar back. Only the C file changes.
 *
 * Build as a shared library:  cc -O3 -fPIC -shared ffi_offload.c -o libaccel.so
 */
#include <stdlib.h>

#define ACCEL_MAX 256
static double* buffers[ACCEL_MAX];
static long    lengths[ACCEL_MAX];
static int     in_use[ACCEL_MAX];

/* Reserve a device buffer of n doubles; returns a handle, or -1 if none are free. */
long long accel_alloc(long long n) {
    for (int h = 0; h < ACCEL_MAX; ++h) {
        if (!in_use[h]) {
            double* p = (double*) calloc((size_t) n, sizeof(double));
            if (!p) return -1;
            buffers[h] = p; lengths[h] = (long) n; in_use[h] = 1;
            return h;
        }
    }
    return -1;
}

/* Write value into every slot of the buffer (stands in for a host->device copy). */
void accel_fill(long long h, double value) {
    if (h < 0 || h >= ACCEL_MAX || !in_use[h]) return;
    for (long i = 0; i < lengths[h]; ++i) buffers[h][i] = value;
}

/* Scale every slot by k in place. */
void accel_scale(long long h, double k) {
    if (h < 0 || h >= ACCEL_MAX || !in_use[h]) return;
    for (long i = 0; i < lengths[h]; ++i) buffers[h][i] *= k;
}

/* The heavy kernel: inner product of two buffers, run `reps` times. On a GPU this is where
 * the launch would happen; here it is a plain loop so the example runs anywhere. */
double accel_dot(long long a, long long b, long long reps) {
    if (a < 0 || a >= ACCEL_MAX || b < 0 || b >= ACCEL_MAX || !in_use[a] || !in_use[b]) return 0.0;
    long n = lengths[a] < lengths[b] ? lengths[a] : lengths[b];
    const double* x = buffers[a];
    const double* y = buffers[b];
    double acc = 0.0;
    for (long long r = 0; r < reps; ++r) {
        double s = 0.0;
        for (long i = 0; i < n; ++i) s += x[i] * y[i];
        acc += s;
    }
    return acc;
}

/* Release a buffer (stands in for a device free). */
void accel_free(long long h) {
    if (h < 0 || h >= ACCEL_MAX || !in_use[h]) return;
    free(buffers[h]); buffers[h] = 0; lengths[h] = 0; in_use[h] = 0;
}
