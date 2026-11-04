#include <cstdio>
#include <vector>
int main(){ long n=5000000; std::vector<bool> f; for(long i=0;i<=n;i++) f.push_back(true); long c=0; for(long i=2;i<=n;i++){ if(f.at(i)){ c++; for(long j=i*i;j<=n;j+=i) f.at(j)=false; } } printf("%ld\n",c); }
