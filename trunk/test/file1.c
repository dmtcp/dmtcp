#include <stdio.h>
#define __USE_GNU
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main()
{
    int count = 0;
    int fd;
    FILE *fp;
    char filename[] = "/tmp/ff_jdl_XXXXXX";
    FILE *fp2;

    fp2 = fopen("/proc/self/exe", "r");
    if (fp2 == NULL)
      abort();

    fd = open(mktemp(filename), O_WRONLY|O_CREAT, S_IWUSR|S_IWUSR);
    if (fd == -1)
      abort();
    // Problematic only when in "w" mode or "a". All + modes and "r" are fine.
    fp = fdopen(fd, "w");
    //fp = fopen("/tmp/ff_jdl", "w");

    fprintf(stdout, "Opened ff_jdl\n");
    fprintf(stdout, "Deleting ff_jdl\n");
    unlink(filename);

    while (1) {
      printf("%d ", count);
      fprintf(fp, "%d", count++);
    }

    fprintf(stdout, "I have returned\n");
    sleep(2);
    return 0;
}
