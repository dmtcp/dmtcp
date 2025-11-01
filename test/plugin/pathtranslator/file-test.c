
/* This test is based on a program suggested by
 * Ankit Garg (Github ID: ankitcse07)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "dmtcp.h"

int
main(int argc, char *argv[])
{
  if (dmtcp_is_enabled()) {
    printf("DMTCP is present\n");
  } else {
    printf("DMTCP is not present\n");
  }

  FILE *fp = fopen("/tmp/msg", "w");
  int counter = 0;

  while (1) {
    sleep(1);
    fprintf(fp, "Write counter value = %d to file\n", counter);
    fprintf(stdout, "Write counter value = %d to file\n", counter++);
    fflush(fp);

    if (counter == 5) {
      printf("Prior to dmtcp_checkpoint\n");
      int ret  = dmtcp_checkpoint();
      if (ret == DMTCP_NOT_PRESENT) {
        printf("Checkpoint not successful\n");
      } else if (ret == DMTCP_AFTER_CHECKPOINT){
        printf("Checkpoint successful \n");
        exit(0);
      } else if (ret == DMTCP_AFTER_RESTART) {
        printf("After restart \n");
      } else {
        printf("Unknown state... exiting now!\n");
        exit(-1);
      }
      fclose(fp);
      fp = fopen("/tmp/msg", "a");
    }

    if (counter == 10) {
      printf("Exiting now!\n");
      fclose(fp);
      if (truncate("/tmp/msg", 0) < 0) {
         perror("truncate: ");
         exit(-1);
      }
      exit(0);
    }
  }

  return 0;
}
