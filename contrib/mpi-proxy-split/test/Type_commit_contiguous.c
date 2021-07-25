/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Type_commit.html
*/
#include <mpi.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char *argv[])
{
  int myrank;
  MPI_Status status;
  MPI_Datatype type;
  int buffer[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  int buf[10];

  MPI_Init(&argc, &argv);

  MPI_Type_contiguous( 10, MPI_INT, &type );
  MPI_Type_commit(&type);
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  if (myrank == 0)
  {
    MPI_Send(buffer, 1, type, 1, 123, MPI_COMM_WORLD);
  }
  else if (myrank == 1)
  {
    MPI_Recv(buf, 1, type, 0, 123, MPI_COMM_WORLD, &status);
    for (int i = 0; i < 10 ; i++) {
      printf("[Rank = %d] got => %d \t expected => %d \n",
             myrank, buf[i], buffer[i]);
      fflush(stdout);
      assert(buffer[i] == buf[i]);
    }
  }
  MPI_Finalize();
  return 0;
}
