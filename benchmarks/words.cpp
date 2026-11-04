#include <cstdio>
#include <string>
#include <unordered_map>
int main(){ std::unordered_map<std::string,long> c; for(long i=0;i<2000000;i++){ std::string k="k"+std::to_string(i%1000); auto it=c.find(k); if(it!=c.end()) it->second++; else c[k]=1; } printf("%zu\n%ld\n",c.size(),c["k7"]); }
