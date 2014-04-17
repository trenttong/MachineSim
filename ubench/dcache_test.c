#include<stdio.h>

void foo()
{
printf("in foo. but foo does nothing\n");
}

#define MAX_ELEM_COUNT (2*1024*1024)

void longexec()
{
printf("in longexec\n");
int array[MAX_ELEM_COUNT];

unsigned long curr_exec = 0;
unsigned long sum_exec = 0;
for(curr_exec = 0; curr_exec < MAX_ELEM_COUNT; curr_exec ++)
   {
   sum_exec += array[curr_exec];
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
printf("starting dcache testing\n");
test();
printf("finishing dcache testing\n");
return 0;
}
