#include "mpi.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int iterations = 100;

int main( int argc, char *argv[] )
{
  int provided, flag, claimed;
  int comm1_counter = 0;
  int comm2_counter = 0;

  int ret = MPI_Init( 0, 0 );
  if (ret != MPI_SUCCESS) {
    printf("MPI_Init failed\n");
    exit(1);
  }

  int rank, nprocs;
  MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  printf("Hello, world.  I am %d of %d\n", rank, nprocs);fflush(stdout);
  if (nprocs < 3) {
    printf("This test needs at least 3 ranks.\n");
    MPI_Finalize();
    return 1;
  }
  int group1_ranks[] = {0, 1};
  int group2_ranks[10000];
  int i,j;
  for (i = 1; i < nprocs; i++) {
    group2_ranks[i-1] = i;
  }

  MPI_Group world_group;
  MPI_Group group1;
  MPI_Group group2;
  MPI_Comm_group(MPI_COMM_WORLD, &world_group);
  MPI_Group_incl(world_group, 2, group1_ranks, &group1);
  MPI_Group_incl(world_group, nprocs-1, group2_ranks, &group2);

  MPI_Comm comm1; // Set to MPI_COMM_NULL by default.
  MPI_Comm comm2; // Set to MPI_COMM_NULL by default.
  MPI_Comm_create(MPI_COMM_WORLD, group1, &comm1);
  MPI_Comm_create(MPI_COMM_WORLD, group2, &comm2);

  //===============================================================
  // Coll. 1:    === |
  //             | | |
  // Coll. 2:    --- |
  // Coll. 2:    === |
  //             | | |
  // Coll. 2:    --- |
  // Coll. 3:    === |
  //             | | |
  // Coll. 4:    --- |
  // Coll. 4:    === |
  //             | | |
  // Coll. 5:    | ===
  //             | | |
  // Coll. 6:    --- |
  // Coll. 6:    === |
  //             | | |
  // Coll. 7:    | ---
  // Coll. 7:    | ===
  // NEW RULE:  For a given communicator, if a free pass is given out so that
  //            all ranks can progress to PHASE 2, then that communicator
  //            will employ a trivial barrier the next time that ranks
  //            enter a wrapper using that communicator.  We then go back to
  //            the classical behavior:
  //              For a given communicator, either:
  //            (a) some ranks have not yet entered PHASE 1 and none are
  //                in PHASE 2 (so ready for checkpoint); or else
  //            (b) some ranks have entered PHASE 2 because of a free pass
  //                (and so we know that all ranks have completed PHASE 1, and
  //                we just need to wait until they all complete PHASE 2

  for (i = 0; i < iterations; i++) {
    if (comm1 != MPI_COMM_NULL) {
      for (j = 0; j < 3; j++) {
        comm1_counter++;
        printf("Rank %d entering comm1, iteration %d\n", rank, comm1_counter);
        fflush(stdout);
        MPI_Barrier(comm1);
        printf("Rank %d leaving comm1, iteration %d\n", rank, comm1_counter);
        fflush(stdout);
        sleep(1);
      }
    }
    if (comm2 != MPI_COMM_NULL) {
      comm2_counter++;
      printf("Rank %d entering comm2, iteration %d\n", rank, comm2_counter);
      fflush(stdout);
      MPI_Barrier(comm2);
      printf("Rank %d leaving comm2, iteration %d\n", rank, comm2_counter);
      fflush(stdout);
      sleep(1);
    }
  }

  MPI_Finalize();
  return 0;
}

