#include <mpi.h>
#include <stdio.h>
#include <assert.h>
int main()
{
  int rank, size;
  MPI_Comm comm;

  MPI_Init(NULL, NULL);
  MPI_Comm_dup( MPI_COMM_WORLD, &comm );
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );
  MPI_Comm_free( &comm );
  if (comm != MPI_COMM_NULL) {
    printf( "[Rank = %d] Freed comm was not set to COMM_NULL\n", rank);
    fflush(stdout);
    assert(0);
  }
  if (rank == 0) {
    printf("Test Passed!\n");
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
