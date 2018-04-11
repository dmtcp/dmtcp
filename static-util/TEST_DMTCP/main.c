#include <stdio.h>
#include <unistd.h>
int main()
{
  int i = 0;
  close(2);
  while (1)
  {
    printf("%d ", i++);
    fflush(stdout);
    sleep(1);
  }
}
