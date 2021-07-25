#include <mpi.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

int main( int argc, char **argv )
{
  int send, recv[3];
  int rank, size;

  MPI_Init( &argc, &argv );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
  MPI_Comm_size( MPI_COMM_WORLD, &size );
  printf("size = %d", size);
  if (size != 3 && rank == 0)
  {
    printf("please run with 3 processes!\n");
    exit(1);
  }

  send = rank + 1;

  MPI_Gather(&send, 1, MPI_INT, &recv, 1, MPI_INT, 0, MPI_COMM_WORLD);

  if(rank == 0) {
    printf("recv = %d %d %d", recv[0], recv[1], recv[2]);
    for(int i = 0; i < 3; i++) {
      assert(recv[i] == i+1);
    }
  }
  fflush(stdout);
  MPI_Finalize();
  return 0;
}
