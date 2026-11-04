#include <cstdio>
#include <cstdint>
static int64_t score(int64_t n){ if(n>=0 && n<100000000) return (n*48271+17)%2147483647; return 0; }
int main(){ int64_t t=0; for(int64_t i=0;i<12000000;i++){ if(__builtin_add_overflow(t,score(i),&t)) return 1; } printf("%lld\n",(long long)t); }
