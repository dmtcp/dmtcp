#include <unistd.h>

int
main()
{
  alarm(15);
  while (1);
  return 0;
}
