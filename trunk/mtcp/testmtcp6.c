/* Compile:
 * gcc -o readline -Wl,--export-dynamic THIS_FILE -lreadline -lhistory -lcurses
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "mtcp.h"

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

int main() {
  char *input = NULL;
  char *prompt = "> ";

  mtcp_init ("testmtcp6.mtcp", 10, 0);
  mtcp_ok ();

  while (1) {
    free(input);
    input = readline(prompt);
    add_history(input);
    if ((0 == strcmp(input, "exit")) | (0 == strcmp(input, "quit")))
      return 0;
  }
  return 1; /* Never reaches this */
}
