#include <mpi.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

int main(int argc, char ** argv)
{
  int rank, size, recv;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  MPI_Scan(&rank, &recv, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  int expected_sum = 0;
  for (int i = 0; i <= rank; i++) {
    expected_sum += i;
  }
  printf("[Rank %d] => recveived sum = %d, expected sum = %d\n",
         rank, recv, expected_sum);
  fflush(stdout);
  assert(recv == expected_sum);
  MPI_Finalize();
}
