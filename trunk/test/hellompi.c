#include <stdio.h>
#include "mpi.h"

int main(int argc, char* argv[])
{
  int rank;
  int size;
  int count = 1;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  printf("Hello, world, I am %d of %d\n", rank, size);

  while(1)
  {
    printf(" %2d ",count++);
    fflush(stdout);
    sleep(2);
  }
  return 0;
}
