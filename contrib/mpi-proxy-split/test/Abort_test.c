/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Abort.html
*/
#include <mpi.h>
#include <stdio.h>
#include <assert.h>
int main(int argc, char *argv[])
{
    MPI_Init(NULL, NULL);
    /*
     MPI_Abort does not return so any return value is erroneous,
     even MPI_SUCCESS.
    */
    int ret = 1234; // random number
    ret = MPI_Abort(MPI_COMM_WORLD, 911);
    /* No further code will execute */
    printf("Return value =%d", ret);
//    assert(ret == 1234);
    printf("MPI Abort implementation Failed\n");
    fflush(stdout);
    MPI_Finalize();
    return 0;
}
