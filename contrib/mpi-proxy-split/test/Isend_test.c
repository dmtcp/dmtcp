#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

int main(int argc, char** argv) {
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // We are assuming at least 2 processes for this task
  if (world_size < 2) {
    fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  int rank = 0;
  int number = 11223344;
  MPI_Request reqs[world_size];
  for (rank = 0; rank < world_size; rank++)
  {
    if (rank == world_rank)
      continue;
    MPI_Isend(&number, 1, MPI_INT, rank, 0, MPI_COMM_WORLD, &reqs[rank]);
  }
  printf("%d sleeping\n", world_rank);
  fflush(stdout);
  sleep(5);
  for (rank = 0; rank < world_size; rank++)
  {
    if(rank == world_rank)
      continue;
    int flag = 0;
    while (!flag) {
      MPI_Test(&reqs[rank], &flag, MPI_STATUS_IGNORE);
    }
  }
    
  for (rank = 0; rank < world_size; rank++)
  {
    if(rank == world_rank)
      continue;
    MPI_Recv(&number, 1, MPI_INT, rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    assert(number == 11223344);
    printf("%d received %d from %d\n", world_rank, number, rank);
    fflush(stdout);
  }
  MPI_Finalize();
}
