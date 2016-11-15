/* To compile, use -DLIB3 to create libdlopen-lib3.so, -DLIB4 for
 * libdlopen-lib4.so, and define neither to create executable.
 * To run, do:  LD_LIBRARY_PATH=. ./dlopen2
 */

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if !defined(LIB3) && !defined(LIB4)
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
      handle = dlopen("libdlopen-lib3.so", RTLD_NOW);
      if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        exit(1);
      }

      /* See 'man dlopen' for example:  POSIX.1-2002 prefers this workaround */
      *(void **)(&fnc) = dlsym(handle, "fnc");
    }

    if (lib == 2) {
      handle = dlopen("libdlopen-lib4.so", RTLD_LAZY);
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

#elif defined(LIB3)
extern "C" int
fnc(int result[2])
{
  return ++(result[0]);
}

extern "C" int
print_constructor()
{
  int dummy = system("echo '    In LIB3::print_constructor'");

  if (dummy == -1) {
    perror("system failed.");
  }
  sleep(1);
  return 0;
}

#elif defined(LIB4)
extern "C" int
fnc(int result[2])
{
  return ++(result[1]);
}

extern "C" int
print_constructor()
{
  int dummy = system("echo '    In LIB4::print_constructor'");

  if (dummy == -1) {
    perror("system failed.");
  }
  sleep(1);
  return 0;
}
#endif // if !defined(LIB3) && !defined(LIB4)
