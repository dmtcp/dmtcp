#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char ** argv)
{
  int rank, comm_size;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  printf("comm_size = %d\n", comm_size);
  sleep(2);

  assert(comm_size > 0);
  int send_buf[2];
  for (int i = 0; i < 2; i++)
  {
    send_buf[i] = (rank+1)*100 + i + 1;
  }
  int recv_buf[comm_size * 2];
  MPI_Allgather(&send_buf, 2, MPI_INT, &recv_buf, 2, MPI_INT, MPI_COMM_WORLD);

  for (int i = 0; i < 2; i++)
  {
    assert(recv_buf[rank * 2 + i] == send_buf[i]);
  }

  for (int i = 0; i < comm_size * 2 ; i++)
  {
    printf("[Rank = %d]: receive buffer[%d] = %d\n",rank, i, recv_buf[i]);
    fflush(stdout);
  }

  MPI_Finalize();
  return 0;
}
