/* This test is based on a program suggested by
 * Ankit Garg (Github ID: ankitcse07)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "dmtcp.h"
#include "pathvirt.h"

int
main(int argc, char *argv[])
{
  FILE *fp = fopen("/tmp/restart_process", "w");
  int counter = 0;

  if (get_original_path_prefix_list != NULL) {
    fprintf(stdout, "Original path prefix: %s\n", get_original_path_prefix_list());
    fprintf(stdout, "New path prefix: %s\n", get_new_path_prefix_list());
  }

  while (1) {
    sleep(1);
    fprintf(fp, "Write counter value = %d to file\n", counter);
    fprintf(stdout, "Write counter value = %d to file\n", counter++);
    fflush(fp);

    if (counter == 10) {
      printf("Exiting now!\n");
      fclose(fp);
      exit(0);
    }
  }

  return 0;
}
