/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Gatherv.html
*/
#include <mpi.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char *argv[])
{
  int buffer[6];
  int rank, size, i;
  int receive_counts[4] = { 0, 1, 2, 3 };
  int receive_displacements[4] = { 0, 0, 1, 3 };

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if (size != 4)
  {
    if (rank == 0)
    {
      printf("Please run with 4 processes\n");fflush(stdout);
    }
    MPI_Finalize();
    return 0;
  }
  /*
    Rank 0: Buffer[] = {0, 0, 0, 0, 0, 0} recv_count = 0, root-recv = []
    Rank 1: Buffer[] = {1, 1, 1, 1, 1, 1} recv_count = 1, displs = 0, root-recv = [1]
    Rank 2: Buffer[] = {2, 2, 2, 2, 2, 2} recv_count = 2, displs = 1, root-recv = [2, 2]
    Rank 3: Buffer[] = {3, 3, 3, 3, 3, 3} recv_count = 3, displs = 3, root-recv = [3, 3, 3]
  */
  for (i=0; i<rank; i++)
  {
    buffer[i] = rank;
  }
  int expected_buf[6] = {1, 2, 2, 3, 3, 3};
  MPI_Gatherv(buffer, rank, MPI_INT, buffer, receive_counts, receive_displacements, MPI_INT, 0, MPI_COMM_WORLD);
  if (rank == 0)
  {
    for (i=0; i < 6; i++)
    {
      printf("buffer[%d] = %d, expected = %d\n", i, buffer[i], expected_buf[i]);
      assert(buffer[i] == expected_buf[i]);
    }
    printf("\n");
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
