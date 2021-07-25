#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ucontext.h>

#include "mpi_copybits.h"

int main(int argc, char **argv, char **envp)
{
  if (argc >= 1) { // run standalone, if no pipefd
    // lh_info is defined in split-process.cpp
    DLOG(INFO, "startText: %p, endText: %p, endOfHeap: %p\n",
         lh_info.startText, lh_info.endText, lh_info.endOfHeap);
    // We're done initializing; jump back to the upper half
    // g_appContext would have been set by the upper half
    int ret = setcontext(lh_info.g_appContext);
    if (ret < 0) {
      DLOG(ERROR, "setcontext failed: %s", strerror(errno));
    }
    return 0;
  }

  return 0;
}
