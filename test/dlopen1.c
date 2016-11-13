/* To compile, use -DLIB1 to create libdlopen-lib1.so, -DLIB2 for
 *   libdlopen-lib2.so, and define neither to create executable.
 * To run, do:  LD_LIBRARY_PATH=. ./dlopen1
 */

#if !defined(LIB1) && !defined(LIB2)
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

int (*fnc)(int result[2]);

int
main(int argc, char *argv[])
{
  int lib = 1;
  void *handle = NULL;
  int result[2] = { 0, 0 };
  int i, answer;
  int cnt1 = 0, cnt2 = 0;

  printf("0: "); fflush(stdout);
  while (1) {
    if (handle != NULL) {
      dlclose(handle);
    }

    if (lib == 1) {
      handle = dlopen("libdlopen-lib1.so", RTLD_NOW);
      if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        exit(1);
      }

      /* See 'man dlopen' for example:  POSIX.1-2002 prefers this workaround */
      *(void **)(&fnc) = dlsym(handle, "fnc");
    }

    if (lib == 2) {
      handle = dlopen("libdlopen-lib2.so", RTLD_LAZY);
      if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        exit(1);
      }

      /* See 'man dlopen' for example:  POSIX.1-2002 prefers this workaround */
      *(void **)(&fnc) = dlsym(handle, "fnc");
    }

    assert(lib == 1 || lib == 2);
    for (i = 0; i < 5; i++) {
      answer = fnc(result);
      if (answer != result[lib - 1]) {
        fprintf(stderr, "lib %d returned wrong answer.\n", lib);
        exit(1);
      }
    }
    if (++cnt1 % 1000 == 0) {
      cnt2++;
      cnt1 = 0;
      printf(".");
      if (cnt2 % 50 == 0) {
        printf("\n%d: ", cnt2 / 50);
      }
      fflush(stdout);
    }
    lib = 3 - lib; /* switch libraries to load */
  }
  return 0;
}

#elif defined(LIB1)
int
fnc(int result[2])
{
  return ++(result[0]);
}

#elif defined(LIB2)
int
fnc(int result[2])
{
  return ++(result[1]);
}
#endif /* if !defined(LIB1) && !defined(LIB2) */
