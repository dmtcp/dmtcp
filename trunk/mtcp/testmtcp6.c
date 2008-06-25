/* Compile:
 * gcc -o readline -Wl,--export-dynamic THIS_FILE -lreadline -lhistory -lcurses
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "mtcp.h"

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
