#include <mpi.h>
#include <stdio.h>
#include <assert.h>
int main()
{
  int rank, size, result;
  MPI_Comm comm;

  MPI_Init(NULL, NULL);
  MPI_Comm_dup( MPI_COMM_WORLD, &comm );
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );
  MPI_Comm_compare(MPI_COMM_WORLD, comm, &result);
  if (!result) {
    printf("[Rank = %d] Comm compare fail! comm = %d, MPI_COMM_WORLD = %d\n",
           rank, comm, MPI_COMM_WORLD);
    assert(0);
  }
  MPI_Finalize();
  return 0;
}
