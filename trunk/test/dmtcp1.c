#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
  char buf [1024 * 1024];
  int count = 1;

  while (1)
  {
	  printf(" %2d ",count++);
	  fflush(stdout);
	  sleep(2);
  }
  return 0;
}
