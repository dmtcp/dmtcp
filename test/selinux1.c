// _DEFAULT_SOURCE for mkstemp  (WHY?)
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

// For open()
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <stdio.h>
#include <selinux/selinux.h>
#include <selinux/context.h>


#define ATOMIC_SHARED volatile __attribute((aligned))

int
main()
{
  int count = 1;

  while (1) {
    char *current_context_str = NULL;
    if (getcon(&current_context_str) == 0) {
        printf("%d Current SELinux context: %s\n", count, current_context_str);
        freecon(current_context_str);
    } else {
        perror("getcon");
        return 1;
    }

    fflush(stdout);
    sleep(2);
  }

  return 0;
}
