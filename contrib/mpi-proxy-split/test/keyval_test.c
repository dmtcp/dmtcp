#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main( int argc, char *argv[] )
{
  int errs = 0;
  int key[3], attrval[3];
  int i;
  int flag;
  MPI_Comm comm;

  MPI_Init( &argc, &argv );
  comm = MPI_COMM_WORLD;
  /* Create key values */
  for (i=0; i<3; i++) {
    MPI_Comm_create_keyval(MPI_NULL_COPY_FN, MPI_NULL_DELETE_FN,
        &key[i], (void *)0 );
    attrval[i] = 100 + i;
  }

  int *val;
  for (i = 0; i < 3; i++) {
    MPI_Attr_put( comm, key[i], &attrval[i] );
    MPI_Attr_get(comm, key[i], &val, &flag);
    printf("keyval: %lx, attrval: %d\n", key[i], *val);
    fflush(stdout);
  }

  printf("Will now sleep for 20 seconds ...\n");
  fflush(stdout);
  unsigned int remaining = 20;
  while (remaining > 0) {
    remaining = sleep(remaining);
    if (remaining > 0) {
      printf("Signal received; continuing sleep for %d seconds.\n", remaining);
      fflush(stdout);
    }
  }

  for (i = 0; i < 3; i++) {
    MPI_Attr_get(comm, key[i], &val, &flag);
    printf("keyval: %lx, attrval: %d\n", key[i], *val);
    fflush(stdout);
  }

  printf("**** %s is now exiting.\n", argv[0]);
  fflush(stdout);

  for (i=0; i<3; i++) {
    MPI_Comm_free_keyval( &key[i] );
  }
  MPI_Finalize();
  return 0;
}
