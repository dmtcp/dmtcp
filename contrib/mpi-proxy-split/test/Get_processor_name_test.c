/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Get_processor_name.html
*/
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
  int rank, nprocs, len;
  char name[MPI_MAX_PROCESSOR_NAME];

  MPI_Init(&argc,&argv);
  MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Get_processor_name(name, &len);
  printf("Hello, world.  I am %d of %d on %s with length %d\n",
         rank, nprocs, name, len);
  fflush(stdout);
  MPI_Finalize();
  return 0;
}
