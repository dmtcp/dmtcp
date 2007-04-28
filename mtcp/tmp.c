#include <unistd.h>
#include <stdio.h>

int main() {
  int tmp, tmp2, fs;
  asm volatile ("movl %%fs,%0" : "=r" (fs));
  asm volatile ("movl %%fs:0xa8,%0" : "=r" (tmp));

  printf("fs: %d, tmp: %d\n", fs, tmp);

  tmp = 1;
  asm volatile ("movl %0,%%fs:0xa8" : "=r" (tmp));
  asm volatile ("movl %%fs,%0" : "=r" (fs));
  asm volatile ("movl %%fs:0xa8,%0" : "=r" (tmp));
  asm volatile ("movl %%gs:0xa8,%0" : "=r" (tmp2));
  printf("fs: %d, tmp: %d; tmp2: %d\n", fs, tmp, tmp2);

  sleep(7);

  return 0;
}
