#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main()
{
  int counter = 0;

  setenv("ABC", "abc", 1);
  while (1) {
    if (counter++ % 1000000 == 0) {
      printf("b"); fflush(stdout);
    }
    if (strcmp(getenv("ABC"), "abc") != 0) {
      printf("\nEnvironment was not preserved across checkpoint/restart.\n");
      exit(1);
    }
  }
  return 0;
}
