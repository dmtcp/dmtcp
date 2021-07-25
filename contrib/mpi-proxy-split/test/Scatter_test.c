#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <stdlib.h>

int main( int argc, char **argv )
{
  int send[3], recv;
  int rank, size, i;

  MPI_Init( &argc, &argv );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
  MPI_Comm_size( MPI_COMM_WORLD, &size );
  if (size != 3)
  {
    printf("please run with 3 processes!\n");
    exit(1);
  }

  if(rank == 0){
    for(i=0;i<size;i++) {
      send[i] = i+1;
    }
  }

  MPI_Scatter(&send, 1, MPI_INT, &recv, 1, MPI_INT, 0, MPI_COMM_WORLD);

  printf("rank = %d \t recv = %d\n", rank, recv);
  fflush(stdout);
  assert(recv == rank+1);
  MPI_Finalize();
  return 0;
}
