#include<stdio.h>

void foo()
{
printf("in foo. but foo does nothing\n");
}

#define MAX_EXEC_COUNT 4000000

void longexec()
{
printf("in longexec\n");
unsigned long curr_exec = 0;
unsigned long sum_exec = 0;
for(curr_exec = 0; curr_exec < MAX_EXEC_COUNT; curr_exec ++)
   {
   sum_exec += curr_exec;
   }
}

void test()
{
// place the tests here.
foo();
longexec();
}

int main()
{
   printf("starting icache testing\n");
   test();
   printf("finishing icache testing\n");
   return 0;
}
