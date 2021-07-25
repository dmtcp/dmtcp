/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Sendrecv.html
*/
#include <mpi.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char *argv[])
{
    int myid, numprocs, left, right;
    const int send_recv_count = 4;
    int buffer[send_recv_count], buffer2[send_recv_count];
    MPI_Request request;
    MPI_Status status;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);

    right = (myid + 1) % numprocs;
    left = myid - 1;
    if (left < 0)
        left = numprocs - 1;
    int i = 0;
    for (i = 0; i < send_recv_count; i++)
    {
      buffer[i] = 5 * (i + 1);
      buffer2[i] = -1;
    }
//  printf("[Rank = %d]: left = %d right = %d buffer = %s\n",
//         myid, left, right, buffer); fflush(stdout);
    for (int i = 0; i < send_recv_count; i++) {
      printf("Before: [Rank = %d]: buffer = %d  buffer2 = %d\n",
             myid, buffer[i], buffer2[i]);
      fflush(stdout);
    }
    MPI_Sendrecv(buffer, send_recv_count, MPI_INT, left, 123, buffer2,
                 send_recv_count, MPI_INT, right, 123, MPI_COMM_WORLD, &status);

    for (int i = 0; i < send_recv_count; i++) {
      assert(buffer[i] == buffer2[i]);
      printf("After: [Rank = %d]: buffer = %d  buffer2 = %d\n", myid,
             buffer[i], buffer2[i]);
    }
    MPI_Finalize();
    return 0;
}
