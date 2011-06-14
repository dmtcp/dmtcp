#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

int main(int argc, char* argv[])
{
  int rank;
  int size;
  int i = 1;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  printf("Hello, world, I am %d of %d\n", rank, size);

  for (i = 1; i < (int)1e9; i++)
  { int buf;
    MPI_Status status;

    buf = i;
    if (rank == 0) {
      /* Send to neighbor on right */
      MPI_Send(&buf, 1, MPI_INT, (rank+1)%size, 0, MPI_COMM_WORLD);
    }
    /* Receive from neighbor on left */
    MPI_Recv(&buf, 1, MPI_INT, (rank-1+size)%size, 0, MPI_COMM_WORLD, &status);
    if (i != buf) {
      fprintf(stderr, "****** INCORRECT RESULT:  %d\n", i);
      exit(1);
    }
    if (rank != 0) {
      /* Send to neighbor on right */
      MPI_Send(&buf, 1, MPI_INT, (rank+1)%size, 0, MPI_COMM_WORLD);
    }

    if (rank == 0) {
      if (i % (int)1e5 == 0) {printf("."); fflush(stdout);}
      if (i % (int)5e6 == 0) printf("\n");
    }
  }
  MPI_Finalize();
  return 0;
}
