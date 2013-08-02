//compile with: gcc -o dmtcp_nocheckpoint -static dmtcp_nocheckpoint.cpp
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
  unsetenv("LD_PRELOAD");
  if(argc==1){
    fprintf(stderr, "USAGE %s cmd...\n", argv[0]);
    return 1;
  }
  execvp(argv[1], argv+1);
  perror("execvp:");
  return 2;
}
