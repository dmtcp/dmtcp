#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void *threadMain(void *dummy);

#define N 10

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int numWaiting = 0;

int main ()
{
  int x[N], i;
  pthread_t t[N];

  for (i = 0; i < N-1; i ++) {
    x[i]=i;
    if (pthread_create (&t[i], NULL, &threadMain, x+i) < 0) {
      fprintf (stderr, "error creating thread: %s\n", strerror (errno));
      return (-1);
    }
  }
  x[N-1]=N-1;
  threadMain(x+N-1);
  return (0);
}

static void *threadMain (void *_n)
{
  int* n = (int*) _n;
  int count = 0;

  while (1) {
    pthread_mutex_lock(&mutex);
    if(numWaiting > N/2)
      pthread_cond_signal(&cond);
    
    numWaiting++;
    pthread_cond_wait(&cond, &mutex);
    numWaiting--;
    pthread_mutex_unlock(&mutex);

    if(count++ % 1000 == 0)
      printf("thread%3d: %8d\n", *n, count/1000);
  }

  return (NULL);
}
