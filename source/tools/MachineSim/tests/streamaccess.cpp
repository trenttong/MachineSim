#include <cstdlib>
#include <cstdio>

/// @@@ implements a sequential loads and store. expect load/store miss ratio be 4/64.
#define ARRSIZE 8*4096*4096
int main()
{
   int *larr = (int*) malloc(sizeof(int) * ARRSIZE);
   int *sarr = (int*) malloc(sizeof(int) * ARRSIZE);
   for(int i =0; i < ARRSIZE; ++i) larr[i] = sarr[i];
   return 0;
}
