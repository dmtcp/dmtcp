// Call as:  THIS_FILE a

#include <unistd.h>
#include <stdio.h>

int main(int argc, char *argv[3]) {
  // This should allow this program to be called with no arguments.
  // But the 'argv[3]' declaration causes DMTCP to fail because
  //   getenv("DMTCP_HIJACK_LIBS") returns NULL in getUpdatedLdPreload().  Why?
  char init_arg[2] = "a";
  if (argc == 1) {
    argv[1] = init_arg;
    argv[2] = NULL;
  } else {
    sleep(1);
  }

  printf("argv[1]: %s (will exec now.)\n", argv[1]);
  if (*argv[1] < 'z') argv[1][0]++;
  execvp(argv[0], argv);
  return 0;
}
