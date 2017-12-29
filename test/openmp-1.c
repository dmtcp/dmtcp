// gcc -fopenmp THIS_FILE

// or:  icc -openmp THIS_FILE

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#define N 500000

int
run()
{
  int i, nthreads, tid;
  float a[N], b[N], c[N], d[N];

  omp_set_dynamic(1);
  omp_set_num_threads(5);

  /* Some initializations */
  for (i = 0; i < N; i++) {
    a[i] = i * 1.5;
    b[i] = i + 22.35;
    c[i] = d[i] = 0.0;
  }

#pragma omp parallel shared(a,b,c,d,nthreads) private(i,tid)
  {
    tid = omp_get_thread_num();
    if (tid == 0) {
      nthreads = omp_get_num_threads();
      printf("Number of threads = %d\n", nthreads);
    }
    printf("Thread %d starting...\n", tid);

  #pragma omp sections nowait
    {
    #pragma omp section
      {
        printf("Thread %d doing section 1\n", tid);
        for (i = 0; i < N; i++) {
          c[i] = a[i] + b[i];
        }
      }

    #pragma omp section
      {
        printf("Thread %d doing section 2\n", tid);
        for (i = 0; i < N; i++) {
          d[i] = a[i] * b[i];
        }
      }
    }  /* end of sections */

    printf("Thread %d done.\n", tid);
  }  /* end of parallel section */
  return 0;
}

int
main(int argc, char *argv[])
{
  while (1) {
    run();
  }
}
