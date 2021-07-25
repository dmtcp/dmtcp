/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Comm_dup.html
*/
#include <mpi.h>
#include <stdio.h>

int main(int argc, char* argv[] )
{
    MPI_Comm dup_comm_world, world_comm;
    MPI_Group world_group;
    int world_rank, world_size, rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );
    MPI_Comm_size( MPI_COMM_WORLD, &world_size );
    MPI_Comm_dup( MPI_COMM_WORLD, &dup_comm_world );
    /* Exercise Comm_create by creating an equivalent to dup_comm_world (sans attributes) */
    MPI_Comm_group( dup_comm_world, &world_group );
    MPI_Comm_create( dup_comm_world, world_group, &world_comm );
    MPI_Comm_rank( world_comm, &rank );
    if (rank != world_rank) {
        printf( "incorrect rank in world comm: %d\n", rank );fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 3001 );
    }
    printf("[Rank %d] \n", rank);fflush(stdout);
    MPI_Finalize();
    return 0;
}
