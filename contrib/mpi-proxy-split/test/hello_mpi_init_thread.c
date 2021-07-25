#include "mpi.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int main( int argc, char *argv[] )
{
  int errs = 0;
  int provided, flag, claimed;

  int ret = MPI_Init_thread( 0, 0, MPI_THREAD_MULTIPLE, &provided );
  if (ret != MPI_SUCCESS) {
    printf("MPI_Init_thread failed\n");
    exit(1);
  }

  // FIXME: MPI_Is_thread_main is not implemented in wrapper
  /* MPI_Is_thread_main( &flag ); */
  /* if (!flag) { */
  /*   errs++; */
  /*   printf( "This thread called init_thread but Is_thread_main gave false\n" );fflush(stdout); */
  /* } */
  /* MPI_Query_thread( &claimed ); */
  /* if (claimed != provided) { */
  /*   errs++; */
  /*   printf( "Query thread gave thread level %d but Init_thread gave %d\n", claimed, provided );fflush(stdout); */
  /* } */

  int rank, nprocs;
  MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  printf("Hello, world.  I am %d of %d\n", rank, nprocs);fflush(stdout);
  printf("Will now sleep for 500 seconds ...\n");fflush(stdout);
  unsigned int remaining = 300;
  while (remaining > 0) {
    remaining = sleep(remaining);
    if (remaining > 0) {
      printf("Signal received; continuing sleep for %d seconds.\n", remaining);
      fflush(stdout);
    }
  }
  printf("**** %s is now exiting.\n", argv[0]);

  MPI_Finalize();
  return errs;
}

