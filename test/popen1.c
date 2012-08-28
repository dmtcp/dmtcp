#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
  int count = 1;
  FILE *fr;
  FILE *fw;
  int ch;

  while (1)
  {
    fr = popen("ls *", "r");
    if (fr == NULL) {
      perror("popen");
      exit(1);
    }

    fw = popen("sort > /dev/null", "w");
    if (fw == NULL) {
      perror("popen");
      exit(1);
    }

    while ((ch = fgetc(fr)) != EOF) {
      if (fputc(ch, fw) == EOF) {
        perror("fputc");
        exit(1);
      }
      putchar(ch);
    }
    pclose(fr);
    pclose(fw);
    sleep(1);
  }
  return 0;
}
