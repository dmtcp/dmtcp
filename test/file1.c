#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main()
{
    int count = 0;
    FILE *fp;
    // Problematic only when in “w” mode or “a”. All + modes and “r” are fine.
    fp = fopen("/tmp/ff_jdl", "w");

    fprintf(stdout, "Opened ff_jdl\n");
    fprintf(stdout, "Deleting ff_jdl\n");
    unlink("/tmp/ff_jdl");

    while (1) {
      printf("%d ", count);
      fprintf(fp, "%d", count++);
    }

    fprintf(stdout, "I have returned\n");
    sleep(2);
    return 0;
}
