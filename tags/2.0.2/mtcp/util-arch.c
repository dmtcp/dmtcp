#include <stdio.h>

// Print each of the MTCP supported architectures
int main() {
#ifdef __x86_64__
  printf("x86_64\n");
#elif __i386__
  printf("i386\n");
#elif __arm__
  printf("arm\n");
#else
  printf("unknown\n");
  return 1;
#endif

  return 0;
}
