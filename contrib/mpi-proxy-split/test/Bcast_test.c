/*
  Source: http://hpc.mines.edu/examples/examples/mpi/c_ex04.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <math.h>
#include <assert.h>
/************************************************************
This is a simple broadcast program in MPI
************************************************************/

int main(argc,argv)
int argc;
char *argv[];
{
    int i,myid, numprocs;
    int source,count;
    int buffer[4];
    int expected_output[4];
    MPI_Status status;
    MPI_Request request;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);
    source = 0;
    count = 4;
    if(myid == source){
      for(i=0; i<count; i++)
        buffer[i] = i;
    }
    for (i = 0; i < count; i++)
    {
        expected_output[i] = i;
    }
    MPI_Bcast(buffer,count,MPI_INT,source,MPI_COMM_WORLD);
    printf("[Rank = %d]", myid);
    for(i=0;i<count;i++)
    {
      assert(buffer[i] == expected_output[i]);
      printf(" %d", buffer[i]);
    }
    printf("\n");fflush(stdout);
    MPI_Finalize();
}
