#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "dmtcp.h"
#include "delayresume.h"

void *thread_fnc(void *arg)
{
  int count = 1;
  printf("\n");
  while(1) {
    printf(" T:%2d ",count++);
    fflush(stdout);
    sleep(2);
  }
}

int main(int argc, char* argv[])
{
  int count = 1;
  pthread_t t1;

  pthread_create(&t1, NULL, thread_fnc, NULL);

  while (1) {
    printf(" %2d ",count++);
    if (count == 5) {
      int ret = dmtcp_checkpoint_block_resume();
      if (ret == DMTCP_AFTER_CHECKPOINT) {
        printf("\nResuming after checkpoint\n");
        sleep(5); // do some work here, for example, tar up user files
        printf("\nResuming ckpt-initiator after checkpoint\n");
        dmtcp_checkpoint_unblock_resume();
        sleep(5); // exit after sometime
        exit(0);
      } else if (ret == DMTCP_AFTER_RESTART) {
        printf("\nResuming after restart\n");
      } else {
        printf("\nUnknown error!\n");
        exit(-1);
      }
    }
    if (count == 8) {
      exit(0);
    }
    fflush(stdout);
    sleep(2);
  }
  return 0;
}
