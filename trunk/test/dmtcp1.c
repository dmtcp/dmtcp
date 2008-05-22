#include <stdio.h>

/* Compile with  -Wl,--export-dynamic to make these functions visible. */
void dmtcpHookPreCheckpoint() {
  printf("\n%s: %s: about to checkpoint\n", __FILE__, __func__);
}
void dmtcpHookPostCheckpoint() {
  printf("\n%s: %s: done checkpointing\n", __FILE__, __func__);
}
void dmtcpHookRestart() {
  printf("\n%s: %s: restarting\n", __FILE__, __func__);
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
