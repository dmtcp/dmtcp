/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Reduce.html
*/
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
/* A simple test of Reduce with all choices of root process */
int main( int argc, char *argv[] )
{
  int errs = 0;
  int rank, size, root;
  int *sendbuf, *recvbuf, i;
  int minsize = 2, count;
  MPI_Comm comm;

  MPI_Init( &argc, &argv );

  comm = MPI_COMM_WORLD;
  /* Determine the sender and receiver */
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );

  for (count = 1; count < 10; count = count * 2) {
    sendbuf = (int *)malloc( count * sizeof(int) );
    recvbuf = (int *)malloc( count * sizeof(int) );
    for (root = 0; root < size; root ++) {
      for (i=0; i<count; i++) sendbuf[i] = i;
      for (i=0; i<count; i++) recvbuf[i] = -1;
      MPI_Reduce( sendbuf, recvbuf, count, MPI_INT, MPI_SUM, root, comm );
      if (rank == root) {
        for (i=0; i<count; i++) {
          printf("[Rank = %d]: recvd = %d, expected = %d\n",
                 rank, recvbuf[i], i * size);
          fflush(stdout);
          assert(recvbuf[i] == i * size);
        }
      }
    }
    free( sendbuf );
    free( recvbuf );
  }
  MPI_Finalize();
  return errs;
}
