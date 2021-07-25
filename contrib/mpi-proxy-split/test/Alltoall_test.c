/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Alltoall.html
*/
#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#endif

int main( int argc, char *argv[] )
{
  int rank, size;
  int chunk = 10;
  int i;
  int *sb;
  int *rb;
  int status, gstatus;

  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&size);
  for ( i=1 ; i < argc ; ++i ) {
    if ( argv[i][0] != '-' )
      continue;
    switch(argv[i][1]) {
      case 'm':
        chunk = atoi(argv[++i]);
        break;
      default:
        fprintf(stderr, "Unrecognized argument %s\n", argv[i]);fflush(stderr);
        MPI_Abort(MPI_COMM_WORLD,EXIT_FAILURE);
    }
  }
  sb = (int *)malloc(size*chunk*sizeof(int));
  if ( !sb ) {
    perror( "can't allocate send buffer" );fflush(stderr);
    MPI_Abort(MPI_COMM_WORLD,EXIT_FAILURE);
  }
  rb = (int *)malloc(size*chunk*sizeof(int));
  if ( !rb ) {
    perror( "can't allocate recv buffer");fflush(stderr);
    free(sb);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }
  for ( i=0 ; i < size*chunk ; ++i ) {
    sb[i] = rank + 1;
    rb[i] = 0;
  }
// printf("SB\n");
//  for ( i=0 ; i < size*chunk ; ++i ) {
//        printf(" %d", sb[i]);
//      }
  status = MPI_Alltoall(sb, chunk, MPI_INT, rb, chunk, MPI_INT, MPI_COMM_WORLD);
//  printf("[Rank = %d] Status = %d, size = %d, chunk = %d\n",
//         rank, status, size, chunk);fflush(stdout);
  MPI_Allreduce( &status, &gstatus, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
  for ( i=0 ; i < size*chunk ; ++i ) {
     assert(rb[i] == (int)(i/chunk) + 1);
  }
  if (rank == 0) {
    if (gstatus != 0) {
      printf("all_to_all returned %d\n",gstatus);fflush(stdout);
      assert(gstatus == 0);
    }
    else
    {
      for ( i=0 ; i < size*chunk ; ++i ) {
        assert(rb[i] == (int)(i/chunk) + 1);
      }
    }
  }
  printf("[Rank %d]: Test Passed!\n", rank);fflush(stdout);
  free(sb);
  free(rb);
  MPI_Finalize();
  return(EXIT_SUCCESS);
}
