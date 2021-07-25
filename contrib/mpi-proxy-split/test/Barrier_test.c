#include <mpi.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int rank, nprocs;
    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    printf("Entering  %d of %d\n", rank, nprocs);fflush(stdout);
    sleep(rank);
    MPI_Barrier(MPI_COMM_WORLD);
    printf("Everyone should be entered by now. If not then Test Failed!\n");
    fflush(stdout);
    MPI_Finalize();
    return 0;
}
