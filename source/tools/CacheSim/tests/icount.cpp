#include <cstdlib>
#include <cstdio>

/// @@@ implements a sequential loads and store. expect total instruction count to be 144M.
#define ARRSIZE 1024*1024*16
int main()
{
   int *larr = (int*) malloc(sizeof(int) * ARRSIZE);
   for(int i =0; i < ARRSIZE; ++i) larr[i] = 1;
   return 0;
}
