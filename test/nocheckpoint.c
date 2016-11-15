#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// This program will be called as "nocheckpoint"
// and its child will be called as "nocheckpoint --called-as-nocheckpoint"

#define NOCHECKPOINT_FLAG "--called-as-nocheckpoint"
#define NOCHECKPOINT_USER_PRELOAD \
  "--called-as-nocheckpoint-with-user-LD_PRELOAD"

static void call_self_nocheckpoint(char *program);
static void kill_parent();

int
main(int argc, char *argv[])
{
  char *preload = getenv("LD_PRELOAD");

  if (argv[1] && strcmp(argv[1], NOCHECKPOINT_FLAG) == 0) {
    // If dmtcp_nocheckpoint failed to pass on exactly user's LD_PRELOAD
    if (preload && strlen(preload) > 0) {
      kill_parent();
    }
    return 0;
  }

  if (argv[1] && strcmp(argv[1], NOCHECKPOINT_USER_PRELOAD) == 0) {
    // If dmtcp_nocheckpoint failed to pass on exactly user's LD_PRELOAD
    if (preload == NULL || strcmp(preload, "USER_STRING") != 0) {
      kill_parent();
    }
    return 0;
  }

  struct timespec tenth_second;
  tenth_second.tv_sec = 0;
  tenth_second.tv_nsec = 100000000; /* 100,000,000 */
  int i = 0;
  while (1) {
    unsetenv("LD_PRELOAD");
    call_self_nocheckpoint(argv[0]);

    setenv("LD_PRELOAD", "", 1);
    call_self_nocheckpoint(argv[0]);

    setenv("LD_PRELOAD", "USER_STRING", 1);
    call_self_nocheckpoint(argv[0]);

    nanosleep(&tenth_second, NULL);
    if (++i % 20 == 0) {
      printf("%2d ", i / 20);
      fflush(stdout);
    }
  }
  return 0;
}

static void
kill_parent()
{
  char kill_command[100];

  snprintf(kill_command, sizeof(kill_command),
           "kill -9 %ld", (long)getppid());
  assert(system(kill_command) == 0);
}

static void
call_self_nocheckpoint(char *program)
{
  char no_checkpoint_prog[1000] = "bin/dmtcp_nocheckpoint";
  char *args[] = { NULL, NULL, NOCHECKPOINT_FLAG, NULL };

  args[0] = no_checkpoint_prog;
  args[1] = program; // Call this program, but with NOCHECKPOINT_FLAG

  int index = 0;
  int slash = -1;
  for (index = 0; index < strlen(program) && program[index] != '\0'; index++) {
    if (program[index] == '/') {
      slash = index;
    }
  }
  if (slash >= 0) {
    strncpy(no_checkpoint_prog, program, 900);
    strncpy(no_checkpoint_prog + slash + 1, "../bin/dmtcp_nocheckpoint", 900);

    // NOTE:  We already set:  args[0] = no_checkpoint_prog
  }

  int childpid = fork();
  if (childpid > 0) {
    waitpid(childpid, NULL, 0);
  } else {
    close(2); // Loader may print to stderr abbout LD_PRELOAD not found.
    execvp(args[0], args);
    perror("execvp");
  }
}
