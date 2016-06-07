#define _POSIX_C_SOURCE 2
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
  FILE *fr;
  FILE *fw;
  int ch;

  while (1) {
    fr = popen("ls *", "r");
    if (fr == NULL) {
      perror("popen");
      exit(1);
    }
    int fd1 = open("/dev/zero", O_RDONLY);

    fw = popen("sort > /dev/null", "w");
    if (fw == NULL) {
      perror("popen");
      exit(1);
    }
    int fd2 = open("/dev/null", O_RDONLY);

    while ((ch = fgetc(fr)) != EOF) {
      if (fputc(ch, fw) == EOF) {
        perror("fputc");
        exit(1);
      }
      putchar(ch);
    }
    pclose(fr);
    close(fd1);
    pclose(fw);
    sleep(2);
    close(fd2);
  }
  return 0;
}
