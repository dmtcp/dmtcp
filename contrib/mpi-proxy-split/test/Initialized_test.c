/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Initialized.html
*/
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main( int argc, char *argv[] )
{
  int flag, error;

  flag = 0;
  MPI_Initialized(&flag);
  if (flag)
  {
    printf("MPI_Initialized returned true before MPI_Init.\n");
    assert(0);
  }

  MPI_Init(&argc, &argv);

  flag = 0;
  MPI_Initialized(&flag);
  if (!flag)
  {
    printf("MPI_Initialized returned false after MPI_Init.\n");
    fflush(stdout);
    MPI_Abort(MPI_COMM_WORLD, error);
    assert(0);
  }
  MPI_Finalize();
  return 0;
}
