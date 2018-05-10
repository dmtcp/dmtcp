#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
  int count = 1;
  FILE *fp = fopen("/dev/null", "w");
  if (fp == NULL) {
    printf("fopen(/dev/null, ...) failed! \n");
    exit(1);
  }

  while (1) {
    fprintf(fp, "%d\n", count++);
  }
  return 0;
}
