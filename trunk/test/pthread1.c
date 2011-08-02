/* Compile with:  gcc THIS_FILE -lpthread */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

void *start_routine(void*);

int main() {
  pthread_t thread;
  void *arg;

  while (1) {
    arg = malloc(10);
    pthread_create(&thread, NULL, start_routine, arg);
    /* thead will free arg, and pass back to us a different arg */
    pthread_join(thread, &arg);
    free(arg);
  }
}

void *start_routine(void* arg) {
  free(arg);
  void *valuePtr = malloc(20);
  pthread_exit(valuePtr);
}
