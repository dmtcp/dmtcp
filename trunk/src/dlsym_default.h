#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>

/************************************************************************
 * IMPORTANT CAVEATS:
 *   DLSYM_DEFAULT() is effective when called from a library, but not when
 *     called from within the base executable.
 *   Don't use dlsym_default_internal() outside of this macro.
 *   This must be a macro because dlsym() looks one level up in the stack
 *     to decide what library the caller of dlsym() is located in.
 ************************************************************************/

#define DLSYM_DEFAULT(handle,symbol) \
  ({ Dl_info info; \
     void *handle2 = handle; \
     if (handle == RTLD_DEFAULT || handle == RTLD_NEXT) { \
       /* Hack: use dlsym()/dlopen() only to get the lib handle */ \
       /* MUST BE MACRO:  dlsym uses stack to find curr. lib for RTLD_NEXT */ \
       void *tmp_fnc = dlsym(handle2, symbol); \
       dladdr(tmp_fnc, &info); \
       /* Found handle of RTLD_NEXT or RTLD_DEFAULT */ \
       handle2 = dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY); \
     } \
     /* Same signature as dlsym(): */ \
     dlsym_default_internal(handle2, symbol); })

void *dlsym_default_internal(void *handle, const char *symbol);

#ifdef STANDALONE
// For standalone testing.
int main() {
  void *fnc;
  printf("pthread_cond_broadcast (via normal linker): %p\n",
         pthread_cond_broadcast);

  printf("================ dlsym ================\n");
  fnc = dlsym(RTLD_DEFAULT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = dlsym(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  printf("================ dlsym_default ================\n");
  // NOTE: RTLD_DEFAULT would try to use this a.out, and fail to find a library
  // fnc = DLSYM_DEFAULT(RTLD_DEFAULT, "pthread_cond_broadcast");
  // printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = DLSYM_DEFAULT(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  return 0;
}
#endif
