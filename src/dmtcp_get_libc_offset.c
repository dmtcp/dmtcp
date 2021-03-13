#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <assert.h>


// This will write to stdout a long int in binary, that is the offset:
//   &(argv[0]) - &read
// for those function addressess in libc.so
// At runtime, a previous program can find &read in libc via
//   'dlsym(RTLD_NEXT, "read")'
//   since DMTCP never wraps "read".  So, the previous program can call
//   this one as 'popen(THIS_PROGRAM, "r")' to get the offset of &(argv[0])
//   from &read.
int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "USAGE:  %s <libc_fnc>\n", argv[0]);
    exit(1);
  }

  // 'read' and 'write' are never wrapped by DMTCP.
  // So, these will be the landmarks, and we'll compute an offset.
  char* read_ptr = dlsym(RTLD_NEXT, "read");
  
  fflush(stdout);
  FILE *libc_stream =
    popen("ldd /bin/ls | grep  libc.so |"
          " sed -e s'%^.*=> %%' | sed -e 's% .*%%'", "r");
  assert(libc_stream != NULL);
  assert(! ferror(libc_stream));
  char libc_path[1000];
  // size_t rc = fread(libc_path, 1, sizeof(libc_path), libc_stream);
  char *rc_fgets = fgets(libc_path, 1000, libc_stream);
  assert(rc_fgets != NULL);
  int rc_pclose = pclose(libc_stream);
  assert(rc_pclose != -1);
  int i;
  for (i = 0; i < strlen(libc_path); i++) {
    if (libc_path[i] == '\n') {
      libc_path[i] = '\0';
    }
  }
#ifdef STANDALONE
  printf("libc_path: %s\n", libc_path);
#endif

  void* dlopen_handle = dlopen(libc_path, RTLD_NOW);
  assert(dlopen_handle != NULL);
  char* libc_fnc_ptr = dlsym(dlopen_handle, argv[1]);
  assert(libc_fnc_ptr != NULL);

  long int fnc_offset = libc_fnc_ptr - read_ptr;
#ifdef STANDALONE
  printf("Offset of %s from 'read': %ld", argv[1], fnc_offset);
#endif
  assert(write(1, &fnc_offset, sizeof(fnc_offset)) == sizeof(fnc_offset));
  return 0;
}
