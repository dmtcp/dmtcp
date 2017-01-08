#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include "mpi.h"

/*
 * ./hellogrid <numOfIterations> <computeTime> <communicationSize>
 */
int main(int argc, char* argv[])
{
  int rank;
  int size;
  int i = 1;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  int iter = argc > 1 ? atoi(argv[1]) : 1;
  int computeTime = argc > 2 ? atoi(argv[2]) : 1;
  int commsize = argc > 3 ? atoi(argv[3]) : 1;
  int gridsize = (int)sqrt(size);

  int *buf = calloc(commsize, sizeof(int));

  printf("Hello, world, I am %d of %d (grid(%dx%d))\n", rank, size, gridsize, gridsize);
  //MPI_Barrier(MPI_COMM_WORLD);

  for (i = 0; i < iter; i++) {
    MPI_Status status;

    printf("Iteration %d, Rank %d\n", i, rank);

    if (rank % gridsize) {
      // send to neighbor on the left
      printf("(1) %d send to %d\n", rank, rank - 1);
      MPI_Send(buf, commsize, MPI_INT, rank - 1, 0, MPI_COMM_WORLD);
    }
    if (rank >= gridsize) {
      // send to neighbor on the top
      printf("(2) %d send to %d\n", rank, rank - gridsize);
      MPI_Send(buf, commsize, MPI_INT, rank - gridsize, 0, MPI_COMM_WORLD);
    }
    if (((rank + 1) % gridsize)) {
      // send to neighbor on the right
      printf("(3) %d send to %d\n", rank, rank + 1);
      MPI_Send(buf, commsize, MPI_INT, rank + 1, 0, MPI_COMM_WORLD);
    }
    if (rank + gridsize < size) {
      // send to neighbor on the bottom
      printf("(4) %d send to %d\n", rank, rank + gridsize);
      MPI_Send(buf, commsize, MPI_INT, rank + gridsize, 0, MPI_COMM_WORLD);
    }
#if 0
    // Diagonal
    if (rank > gridsize && (rank % gridsize)) {
      // send to neighbor on the top left
      printf("(3) %d send to %d\n", rank, rank - gridsize - 1);
      MPI_Send(buf, 1, MPI_INT, rank - gridsize - 1, 0, MPI_COMM_WORLD);
    }
#endif
    //MPI_Barrier(MPI_COMM_WORLD);

    if (rank % gridsize) {
      // receive from neighbor on the left
      printf("(1) %d recv from %d\n", rank, rank - 1);
      MPI_Recv(buf, commsize, MPI_INT, rank - 1, 0, MPI_COMM_WORLD, &status);
    }
    if (rank >= gridsize) {
      // receive from neighbor on the top
      printf("(2) %d recv from %d\n", rank, rank - gridsize);
      MPI_Recv(buf, commsize, MPI_INT, rank - gridsize, 0, MPI_COMM_WORLD, &status);
    }
    if (((rank + 1) % gridsize)) {
      // receive from neighbor on the right
      printf("(3) %d recv from %d\n", rank, rank + 1);
      MPI_Recv(buf, commsize, MPI_INT, rank + 1, 0, MPI_COMM_WORLD, &status);
    }
    if (rank + gridsize < size) {
      // receive from neighbor on the bottom
      printf("(4) %d recv from %d\n", rank, rank + gridsize);
      MPI_Recv(buf, commsize, MPI_INT, rank + gridsize, 0, MPI_COMM_WORLD, &status);
    }

#if 0
    // Diagonal
    if (((rank + 1) % gridsize) && rank + gridsize < size) {
      // receive from neighbor on the bottom right
      printf("(3) %d recv from %d\n", rank, rank + gridsize + 1);
      MPI_Recv(buf, 1, MPI_INT, rank + gridsize + 1, 0, MPI_COMM_WORLD, &status);
    }
#endif

    sleep(computeTime); // simulate compute after recv

  }

  if (rank == 0) {
    printf("Done\n");
  }

  MPI_Finalize();
  return 0;
}
