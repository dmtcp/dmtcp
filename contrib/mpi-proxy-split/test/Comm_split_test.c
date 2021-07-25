// Author: Wesley Bland
// Copyright 2015 www.mpitutorial.com
// This code is provided freely with the tutorials on mpitutorial.com. Feel
// free to modify it for your own use. Any distribution of the code must
// either provide a link to www.mpitutorial.com or keep this header intact.
//
// Example using MPI_Comm_split to divide a communicator into subcommunicators
//

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <assert.h>

int main(int argc, char **argv) {
  MPI_Init(NULL, NULL);

  // Get the rank and size in the original communicator
  int world_rank, world_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // We are assuming at least 2 processes for this task
  if (world_size % 4 != 0) {
    fprintf(stderr, "World size should be multiple of 4  for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  int color = world_rank / 4; // Determine color based on row

  // Split the communicator based on the color
  // and use the original rank for ordering
  MPI_Comm row_comm;
  MPI_Comm_split(MPI_COMM_WORLD, color, world_rank, &row_comm);

  int row_rank, row_size;
  MPI_Comm_rank(row_comm, &row_rank);
  MPI_Comm_size(row_comm, &row_size);
  assert( row_size == 4);
  assert( row_rank == world_rank % 4);
  printf("WORLD RANK/SIZE: %d/%d --- ROW RANK/SIZE: %d/%d\n",
         world_rank, world_size, row_rank, row_size);
  fflush(stdout);

  MPI_Comm_free(&row_comm);

  MPI_Finalize();
}
