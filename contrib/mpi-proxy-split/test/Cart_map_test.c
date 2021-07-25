/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Cart_map.html
*/
#include <mpi.h>
#include <stdio.h>
#include <assert.h>

int main( int argc, char *argv[] )
{
  int errs = 0;
  int dims[2];
  int periods[2];
  int size, rank, newrank;

  MPI_Init( &argc, &argv );
  MPI_Comm_size( MPI_COMM_WORLD, &size );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );

  /* This defines a cartision grid with a single point */
  periods[0] = 1;
  dims[0] = 1;
  MPI_Cart_map( MPI_COMM_WORLD, 1, dims, periods, &newrank );
  if (rank > 0) {
    if (newrank != MPI_UNDEFINED) {
      errs++;
      printf( "rank outside of input communicator not UNDEFINED\n" );
      fflush(stdout);
    }
  }
  else {
    if (rank != newrank) {
      errs++;
      printf( "Newrank not defined and should be 0\n" );fflush(stdout);
    }
  }
  MPI_Finalize();
  assert(errs == 0);
  return 0;
}
