#include <stdio.h>
/* must compile with -Wl,-export-dynamic for the hook functions to be visible */
void dmtcpHookPreCheckpoint()
{
  printf("\ndmtcp1.c: dmtcpHookPreCheckpoint: about to checkpoint\n");
}
void dmtcpHookPostCheckpoint()
{
  printf("\ndmtcp1.c: dmtcpHookPostCheckpoint: done checkpointing\n");
}
void dmtcpHookRestart()
{
  printf("\ndmtcp1.c: dmtcpHookRestart: restarting\n");
}

int main(int argc, char* argv[])
{
  int count = 1;

  while (1)
  {
	  printf(" %2d ",count++);
	  fflush(stdout);
	  sleep(2);
  }
  return 0;
}
